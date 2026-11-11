#!/usr/bin/env python3
"""Baut ein bootfähiges Disk-Image für UEFI-Rechner.

Aufbau: GPT-Partitionstabelle mit einer EFI-Systempartition (FAT32), darin
die Datei \\EFI\\BOOT\\BOOTX64.EFI - dort sucht jede UEFI-Firmware auf
Wechselmedien nach dem Startprogramm. Braucht nur die Python-Standardbibliothek.

Aufruf: mkimage.py BOOTX64.EFI helloos.img
"""
import struct
import sys
import uuid
import zlib

SECTOR = 512
PART_START = 2048                       # 1 MiB, übliche Ausrichtung
PART_SECTORS = 40 * 2048                # 40 MiB
GPT_ENTRIES = 128
GPT_ENTRY_SECTORS = GPT_ENTRIES * 128 // SECTOR     # 32
TOTAL_SECTORS = PART_START + PART_SECTORS + 1 + GPT_ENTRY_SECTORS + 2047

ESP_TYPE = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")
DISK_GUID = uuid.UUID("48656c6c-6f4f-5320-6469-736b00000001")
PART_GUID = uuid.UUID("48656c6c-6f4f-5320-6573-700000000001")


# --- FAT32 ---------------------------------------------------------------------

RESERVED = 32
FATS = 2


def fat_size(total):
    """Sektoren je FAT, so dass alle Cluster (1 Sektor groß) einen Eintrag haben."""
    size = 1
    while True:
        clusters = total - RESERVED - FATS * size
        need = -(-(clusters + 2) * 4 // SECTOR)
        if need <= size:
            return size
        size = need


def dir_entry(name, attr, cluster, size=0):
    assert len(name) == 11
    date = ((2026 - 1980) << 9) | (1 << 5) | 1
    return struct.pack("<11sBBBHHHHHHHI", name, attr, 0, 0, 0, date, date,
                       cluster >> 16, 0, date, cluster & 0xFFFF, size)


def directory(entries):
    data = b"".join(entries)
    assert len(data) <= SECTOR
    return data.ljust(SECTOR, b"\0")


def make_fat32(payload):
    total = PART_SECTORS
    fatsz = fat_size(total)
    clusters = total - RESERVED - FATS * fatsz
    assert clusters >= 65525, "zu klein für FAT32"

    file_clusters = max(1, -(-len(payload) // SECTOR))
    ROOT, EFI, BOOT, FILE = 2, 3, 4, 5
    assert FILE + file_clusters <= clusters + 2, "Datei passt nicht ins Image"

    # Bootsektor mit BIOS-Parameterblock
    boot = bytearray(SECTOR)
    boot[0:3] = b"\xEB\x58\x90"
    boot[3:11] = b"HELLOOS "
    struct.pack_into("<HBHBHHBHHHII", boot, 11,
                     SECTOR, 1, RESERVED, FATS, 0, 0, 0xF8, 0, 63, 255,
                     PART_START, total)
    struct.pack_into("<IHHIHH", boot, 36, fatsz, 0, 0, ROOT, 1, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into("<I", boot, 67, 0x48454C4F)
    boot[71:82] = b"HELLOOS    "
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xAA"

    used = 3 + file_clusters            # drei Verzeichnisse + Datei
    fsinfo = bytearray(SECTOR)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<III", fsinfo, 484, 0x61417272, clusters - used,
                     FILE + file_clusters)
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)

    # Zuordnungstabelle: Verzeichnisse je ein Cluster, die Datei am Stück
    END = 0x0FFFFFFF
    fat = bytearray(fatsz * SECTOR)
    entries = [0x0FFFFFF8, END, END, END, END]
    entries += [FILE + i + 1 for i in range(file_clusters - 1)] + [END]
    struct.pack_into("<%dI" % len(entries), fat, 0, *entries)

    DIR, ARCHIVE, LABEL = 0x10, 0x20, 0x08
    root = directory([dir_entry(b"HELLOOS    ", LABEL, 0),
                      dir_entry(b"EFI        ", DIR, EFI)])
    efi = directory([dir_entry(b".          ", DIR, EFI),
                     dir_entry(b"..         ", DIR, 0),
                     dir_entry(b"BOOT       ", DIR, BOOT)])
    bootdir = directory([dir_entry(b".          ", DIR, BOOT),
                         dir_entry(b"..         ", DIR, EFI),
                         dir_entry(b"BOOTX64 EFI", ARCHIVE, FILE, len(payload))])

    part = bytearray(total * SECTOR)
    for base in (0, 6):                 # Original und Sicherungskopie
        part[base * SECTOR:(base + 1) * SECTOR] = boot
        part[(base + 1) * SECTOR:(base + 2) * SECTOR] = fsinfo
    for i in range(FATS):
        start = (RESERVED + i * fatsz) * SECTOR
        part[start:start + len(fat)] = fat
    data = (RESERVED + FATS * fatsz) * SECTOR
    content = root + efi + bootdir + payload
    part[data:data + len(content)] = content
    return bytes(part)


# --- GPT -----------------------------------------------------------------------

def gpt_header(current, backup, entries_lba, entries_crc):
    last_usable = TOTAL_SECTORS - GPT_ENTRY_SECTORS - 2
    def pack(crc):
        return struct.pack("<8sIIIIQQQQ16sQIII", b"EFI PART", 0x00010000, 92,
                           crc, 0, current, backup, 2 + GPT_ENTRY_SECTORS,
                           last_usable, DISK_GUID.bytes_le, entries_lba,
                           GPT_ENTRIES, 128, entries_crc)
    header = pack(zlib.crc32(pack(0)))
    return header.ljust(SECTOR, b"\0")


def make_image(payload):
    image = bytearray(TOTAL_SECTORS * SECTOR)
    last = TOTAL_SECTORS - 1

    # Schutz-MBR: sagt alten Programmen "diese Platte ist belegt"
    struct.pack_into("<B3sB3sII", image, 446, 0, b"\x00\x02\x00", 0xEE,
                     b"\xFF\xFF\xFF", 1, min(last, 0xFFFFFFFF))
    image[510:512] = b"\x55\xAA"

    entry = struct.pack("<16s16sQQQ72s", ESP_TYPE.bytes_le, PART_GUID.bytes_le,
                        PART_START, PART_START + PART_SECTORS - 1, 0,
                        "EFI System".encode("utf-16-le"))
    entries = entry.ljust(GPT_ENTRY_SECTORS * SECTOR, b"\0")
    crc = zlib.crc32(entries)

    backup_entries = last - GPT_ENTRY_SECTORS
    image[1 * SECTOR:2 * SECTOR] = gpt_header(1, last, 2, crc)
    image[2 * SECTOR:2 * SECTOR + len(entries)] = entries
    image[backup_entries * SECTOR:last * SECTOR] = entries
    image[last * SECTOR:] = gpt_header(last, 1, backup_entries, crc)

    part = make_fat32(payload)
    image[PART_START * SECTOR:PART_START * SECTOR + len(part)] = part
    return image


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as f:
        payload = f.read()
    with open(sys.argv[2], "wb") as f:
        f.write(make_image(payload))


if __name__ == "__main__":
    main()
