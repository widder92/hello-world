/*
 * AHCI: der Controller für SATA-Platten und -SSDs.
 *
 * Jeder Anschluss ("Port") hat eine Liste von Auftragsplätzen. Ein Auftrag
 * besteht aus einem kleinen Paket für das Laufwerk (FIS) mit dem ATA-Befehl
 * und der Angabe, wo im Speicher die Daten liegen. HelloOS benutzt immer
 * nur den ersten Platz und wartet, bis der Controller ihn als erledigt meldet.
 */
#include "disk.h"
#include "block.h"
#include "console.h"
#include "cpu.h"
#include "memory.h"
#include "pci.h"
#include "string.h"
#include "task.h"
#include "time.h"

#define DISKS_MAX      2
#define PAGE           4096
#define CHUNK_PAGES    16               /* höchstens 64 KiB je Auftrag */
#define CHUNK_SECTORS  (CHUNK_PAGES * PAGE / SECTOR_SIZE)

/* Register des Controllers */
#define HBA_CAP   0x00
#define HBA_GHC   0x04
#define HBA_PI    0x0C
#define HBA_CAP2  0x24
#define HBA_BOHC  0x28
#define GHC_AHCI  0x80000000u

/* Register eines Anschlusses (ab 0x100 + Nummer * 0x80) */
#define PORT_CLB   0x00
#define PORT_FB    0x08
#define PORT_IS    0x10
#define PORT_IE    0x14
#define PORT_CMD   0x18
#define PORT_TFD   0x20
#define PORT_SIG   0x24
#define PORT_SSTS  0x28
#define PORT_SCTL  0x2C
#define PORT_SERR  0x30
#define PORT_CI    0x38
#define CMD_START  0x0001u
#define CMD_SPIN_UP 0x0002u
#define CMD_POWER_ON 0x0004u
#define CMD_FIS_RX 0x0010u
#define CMD_FIS_ON 0x4000u
#define CMD_RUNNING 0x8000u
#define TFD_ERROR  0x01u
#define TFD_BUSY   0x88u                /* BSY oder DRQ */
#define IS_TASK_ERROR 0x40000000u

/* ATA-Befehle */
#define ATA_IDENTIFY    0xEC
#define ATA_READ_EXT    0x25
#define ATA_WRITE_EXT   0x35
#define ATA_FLUSH_EXT   0xEA
#define ATA_STANDBY_NOW 0xE0

typedef struct {
    uint16_t flags;                     /* Länge des FIS, Schreibrichtung */
    uint16_t prd_count;
    uint32_t transferred;
    uint64_t table;                     /* Adresse der Auftragstabelle */
    uint32_t reserved[4];
} slot_t;

typedef struct {
    uint8_t fis[64];
    uint8_t atapi[16];
    uint8_t reserved[48];
    struct {
        uint64_t address;
        uint32_t reserved;
        uint32_t bytes;                 /* Anzahl - 1 */
    } prd[1];
} table_t;

typedef struct {
    volatile uint8_t *port;
    slot_t *slots;
    table_t *table;
    uint8_t *buffer;
    bool busy, dead;
    block_device_t *block;
} disk_t;

static disk_t disks[DISKS_MAX];
static int disk_count;

static uint32_t read32(volatile uint8_t *base, uint32_t reg)
{
    return *(volatile uint32_t *)(base + reg);
}

static void write32(volatile uint8_t *base, uint32_t reg, uint32_t value)
{
    *(volatile uint32_t *)(base + reg) = value;
}

static bool wait_clear(volatile uint8_t *base, uint32_t reg, uint32_t mask,
                       uint32_t timeout_ms)
{
    uint64_t deadline = time_ms() + timeout_ms;
    while (read32(base, reg) & mask) {
        if (time_ms() >= deadline)
            return false;
        cpu_pause();
    }
    return true;
}

/* Führt einen ATA-Befehl aus; bytes Daten stehen im bzw. landen im Zwischenpuffer. */
static bool command(disk_t *d, uint8_t ata, uint64_t lba, uint32_t sectors, size_t bytes,
                    bool write, uint32_t timeout_ms)
{
    if (d->dead)
        return false;
    if (!wait_clear(d->port, PORT_TFD, TFD_BUSY, 5000))
        goto failed;

    memset(d->table, 0, sizeof(*d->table));
    uint8_t *fis = d->table->fis;
    fis[0] = 0x27;                      /* Paket vom Rechner an das Laufwerk */
    fis[1] = 0x80;                      /* ... mit einem Befehl */
    fis[2] = ata;
    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 0x40;                      /* Adressierung über Sektornummern (LBA) */
    fis[8] = (uint8_t)(lba >> 24);
    fis[9] = (uint8_t)(lba >> 32);
    fis[10] = (uint8_t)(lba >> 40);
    fis[12] = (uint8_t)sectors;
    fis[13] = (uint8_t)(sectors >> 8);
    if (bytes) {
        d->table->prd[0].address = (uint64_t)d->buffer;
        d->table->prd[0].bytes = (uint32_t)bytes - 1;
    }
    memset(&d->slots[0], 0, sizeof(slot_t));
    d->slots[0].flags = (uint16_t)(5 | (write ? 0x40 : 0));     /* FIS: 5 Doppelworte */
    d->slots[0].prd_count = bytes ? 1 : 0;
    d->slots[0].table = (uint64_t)d->table;

    write32(d->port, PORT_IS, 0xFFFFFFFFu);     /* alte Meldungen löschen */
    write32(d->port, PORT_CI, 1);
    uint64_t deadline = time_ms() + timeout_ms;
    while (read32(d->port, PORT_CI) & 1) {
        if (read32(d->port, PORT_IS) & IS_TASK_ERROR)
            return false;               /* das Laufwerk meldet einen Fehler */
        if (time_ms() >= deadline)
            goto failed;
        cpu_pause();
    }
    return !(read32(d->port, PORT_IS) & IS_TASK_ERROR) &&
           !(read32(d->port, PORT_TFD) & TFD_ERROR);

failed:
    d->dead = true;                     /* Zustand unklar - lieber nichts mehr schreiben */
    klog("AHCI: keine Antwort vom Laufwerk - Datenträger wird nicht mehr benutzt\n");
    return false;
}

static void lock(disk_t *d)
{
    for (;;) {
        uint64_t flags = irq_save();
        if (!d->busy) {
            d->busy = true;
            irq_restore(flags);
            return;
        }
        irq_restore(flags);
        task_sleep(1);
    }
}

static bool transfer(block_device_t *block, bool write, uint64_t lba, uint32_t count,
                     void *buffer)
{
    disk_t *d = block->driver_data;
    uint8_t *data = buffer;
    bool ok = true;

    lock(d);
    while (ok && count > 0) {
        uint32_t sectors = count < CHUNK_SECTORS ? count : CHUNK_SECTORS;
        size_t bytes = (size_t)sectors * SECTOR_SIZE;
        if (write)
            memcpy(d->buffer, data, bytes);
        ok = command(d, write ? ATA_WRITE_EXT : ATA_READ_EXT, lba, sectors, bytes, write,
                     10000);
        if (ok && !write)
            memcpy(data, d->buffer, bytes);
        data += bytes;
        lba += sectors;
        count -= sectors;
    }
    d->busy = false;
    return ok;
}

static bool ahci_read(block_device_t *block, uint64_t lba, uint32_t count, void *buffer)
{
    return transfer(block, false, lba, count, buffer);
}

static bool ahci_write(block_device_t *block, uint64_t lba, uint32_t count,
                       const void *buffer)
{
    return transfer(block, true, lba, count, (void *)buffer);
}

static bool ahci_flush(block_device_t *block)
{
    disk_t *d = block->driver_data;
    lock(d);
    bool ok = command(d, ATA_FLUSH_EXT, 0, 0, 0, false, 30000);
    d->busy = false;
    return ok;
}

/* Wartet, bis die Verbindung zum Laufwerk steht. */
static bool link_up(volatile uint8_t *port, uint32_t timeout_ms)
{
    uint64_t deadline = time_ms() + timeout_ms;
    while ((read32(port, PORT_SSTS) & 0x0F) != 3) {
        if (time_ms() >= deadline)
            return false;
        cpu_pause();
    }
    return true;
}

static bool start_port(disk_t *d, volatile uint8_t *port, int index)
{
    d->port = port;

    /* Den Anschluss anhalten und eigene Speicherbereiche eintragen */
    write32(port, PORT_CMD, read32(port, PORT_CMD) & ~CMD_START);
    if (!wait_clear(port, PORT_CMD, CMD_RUNNING, 1000))
        return false;
    write32(port, PORT_CMD, read32(port, PORT_CMD) & ~CMD_FIS_RX);
    if (!wait_clear(port, PORT_CMD, CMD_FIS_ON, 1000))
        return false;

    d->slots = alloc_pages(1);
    uint8_t *received = alloc_pages(1);
    d->table = alloc_pages(1);
    d->buffer = alloc_pages(CHUNK_PAGES);
    if (!d->slots || !received || !d->table || !d->buffer)
        return false;
    write32(port, PORT_CLB, (uint32_t)(uint64_t)d->slots);
    write32(port, PORT_CLB + 4, (uint32_t)((uint64_t)d->slots >> 32));
    write32(port, PORT_FB, (uint32_t)(uint64_t)received);
    write32(port, PORT_FB + 4, (uint32_t)((uint64_t)received >> 32));
    write32(port, PORT_IE, 0);          /* keine Interrupts - es wird nachgefragt */
    /* Strom und Anlauf einschalten; Antworten des Laufwerks empfangen */
    write32(port, PORT_CMD, read32(port, PORT_CMD) | CMD_SPIN_UP | CMD_POWER_ON | CMD_FIS_RX);

    /* Steht die Verbindung nicht (mehr), neu aufbauen lassen */
    if (!link_up(port, 20)) {
        write32(port, PORT_SCTL, (read32(port, PORT_SCTL) & ~0x0Fu) | 1);
        time_delay_ms(2);
        write32(port, PORT_SCTL, read32(port, PORT_SCTL) & ~0x0Fu);
        if (!link_up(port, 1500))
            return false;               /* an diesem Anschluss hängt nichts */
    }
    write32(port, PORT_SERR, 0xFFFFFFFFu);
    write32(port, PORT_IS, 0xFFFFFFFFu);
    if (!wait_clear(port, PORT_TFD, TFD_BUSY, 10000)) {
        klog("SATA %d: Laufwerk wird nicht bereit\n", index);
        return false;
    }
    if (read32(port, PORT_SIG) != 0x00000101)
        return false;                   /* keine Platte (z. B. ein CD-Laufwerk) */
    write32(port, PORT_CMD, read32(port, PORT_CMD) | CMD_START);

    /* Was für ein Laufwerk ist das? */
    if (!command(d, ATA_IDENTIFY, 0, 0, SECTOR_SIZE, false, 5000)) {
        klog("SATA %d: Laufwerk beantwortet die Abfrage nicht\n", index);
        return false;
    }
    const uint16_t *id = (const uint16_t *)d->buffer;
    bool lba48 = id[83] & (1u << 10);
    bool odd_sectors = (id[106] & 0xC000) == 0x4000 && (id[106] & (1u << 12));
    uint64_t sectors = id[100] | ((uint64_t)id[101] << 16) | ((uint64_t)id[102] << 32) |
                       ((uint64_t)id[103] << 48);
    if (!lba48 || odd_sectors || sectors == 0) {
        klog("SATA %d: Laufwerk wird nicht unterstützt (zu alt oder keine 512-Byte-Sektoren)\n",
             index);
        return false;
    }
    char model[41];
    for (int i = 0; i < 20; i++) {      /* je zwei Zeichen stehen vertauscht */
        model[2 * i] = (char)(id[27 + i] >> 8);
        model[2 * i + 1] = (char)id[27 + i];
    }
    model[40] = '\0';
    for (int i = 39; i >= 0 && (model[i] == ' ' || model[i] == '\0'); i--)
        model[i] = '\0';

    char name[24];
    ksnprintf(name, sizeof(name), "sata%d", index);
    d->block = block_register_internal(name, sectors);
    if (!d->block)
        return false;
    d->block->driver_data = d;
    d->block->read = ahci_read;
    d->block->write = ahci_write;
    d->block->flush = ahci_flush;
    klog("SATA %d: %s, %llu MiB (schreibgeschützt bis auf eine HelloOS-Partition)\n", index,
         model, sectors / 2048);
    return true;
}

void ahci_init(void)
{
    pci_device_t found[2];
    int count = pci_find(0x01, 0x06, 0x01, found, 2);

    for (int c = 0; c < count; c++) {
        uint64_t bar = pci_read32(&found[c], 0x24) & ~0xFull;
        if (bar == 0)
            continue;
        pci_enable(&found[c]);
        vmm_map(bar, 0x2000);
        volatile uint8_t *hba = (volatile uint8_t *)bar;

        /* Die Firmware gibt den Controller ab, falls sie ihn noch festhält. */
        if (read32(hba, HBA_CAP2) & 1) {
            write32(hba, HBA_BOHC, read32(hba, HBA_BOHC) | 2);
            wait_clear(hba, HBA_BOHC, 1, 2000);
        }
        write32(hba, HBA_GHC, read32(hba, HBA_GHC) | GHC_AHCI);
        write32(hba, HBA_GHC, read32(hba, HBA_GHC) & ~2u);      /* Interrupts aus */

        uint32_t implemented = read32(hba, HBA_PI);
        for (int p = 0; p < 32 && disk_count < DISKS_MAX; p++) {
            if (!(implemented & (1u << p)))
                continue;
            volatile uint8_t *port = hba + 0x100 + p * 0x80;
            disk_t *d = &disks[disk_count];
            memset(d, 0, sizeof(*d));
            if (start_port(d, port, disk_count))
                disk_count++;
            else
                d->dead = true;
        }
    }
}

void nvme_shutdown(void);

void disk_shutdown(void)
{
    for (int i = 0; i < disk_count; i++) {
        disk_t *d = &disks[i];
        if (d->dead || d->busy)
            continue;
        command(d, ATA_FLUSH_EXT, 0, 0, 0, false, 10000);
        command(d, ATA_STANDBY_NOW, 0, 0, 0, false, 5000);     /* Köpfe parken */
    }
    nvme_shutdown();
}
