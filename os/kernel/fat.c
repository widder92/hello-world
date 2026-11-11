#include "fat.h"
#include "memory.h"
#include "string.h"

#define CACHE_SECTORS  64
#define RUN_SECTORS    64               /* so viel am Stück lesen/schreiben */
#define FAT_END        0x0FFFFFF8u      /* ab hier: Ende der Kette */
#define FAT_MASK       0x0FFFFFFFu
#define ATTR_LABEL     0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_LONG_NAME 0x0F

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    wr16(p, (uint16_t)v);
    wr16(p + 2, (uint16_t)(v >> 16));
}

/* --- Einhängen --------------------------------------------------------------------- */

/* Prüft, ob bei Sektor start ein FAT32-Dateisystem beginnt. */
static bool try_volume(fat_t *fs, block_device_t *dev, uint64_t start)
{
    uint8_t boot[SECTOR_SIZE];
    if (!block_read(dev, start, 1, boot))
        return false;
    if (boot[510] != 0x55 || boot[511] != 0xAA)
        return false;

    uint32_t per_cluster = boot[13];
    uint32_t reserved = rd16(boot + 14);
    uint32_t fat_count = boot[16];
    uint32_t total = rd32(boot + 32);
    uint32_t fat_sectors = rd32(boot + 36);
    /* FAT32 erkennt man an: kein festes Wurzelverzeichnis, FAT-Größe nur 32-bittig */
    if (rd16(boot + 11) != SECTOR_SIZE || rd16(boot + 17) != 0 ||
        rd16(boot + 22) != 0 || fat_sectors == 0 || fat_count == 0 ||
        reserved == 0 || per_cluster == 0 ||
        (per_cluster & (per_cluster - 1)) != 0)
        return false;

    uint32_t data_start = reserved + fat_count * fat_sectors;
    if (total <= data_start)
        return false;

    memset(fs, 0, sizeof(*fs));
    fs->dev = dev;
    fs->start = start;
    fs->sectors_per_cluster = per_cluster;
    fs->fat_start = reserved;
    fs->fat_sectors = fat_sectors;
    fs->fat_count = fat_count;
    fs->data_start = data_start;
    fs->root_cluster = rd32(boot + 44);
    fs->cluster_count = (total - data_start) / per_cluster;
    fs->info_sector = rd16(boot + 48);

    /* Die FAT muss für alle Cluster einen Eintrag haben. */
    if ((uint64_t)(fs->cluster_count + 2) * 4 >
        (uint64_t)fat_sectors * SECTOR_SIZE)
        return false;
    fs->cache = kmalloc(CACHE_SECTORS * SECTOR_SIZE);
    return fs->cache != NULL;
}

bool fat_mount(fat_t *fs, block_device_t *dev)
{
    uint8_t sector[SECTOR_SIZE];
    memset(fs, 0, sizeof(*fs));

    /* 1. Datenträger ohne Partitionstabelle */
    if (try_volume(fs, dev, 0))
        return true;
    if (!block_read(dev, 0, 1, sector) || sector[510] != 0x55 ||
        sector[511] != 0xAA)
        return false;

    /* 2. Klassische Partitionstabelle (MBR): vier Einträge ab Byte 446 */
    bool gpt = false;
    for (int i = 0; i < 4; i++) {
        const uint8_t *entry = sector + 446 + 16 * i;
        uint32_t first = rd32(entry + 8);
        if (entry[4] == 0xEE)
            gpt = true;                 /* Schutzeintrag: eigentlich GPT */
        else if (entry[4] != 0 && first != 0 && try_volume(fs, dev, first))
            return true;
    }
    if (!gpt)
        return false;

    /* 3. GPT: Kopf in Sektor 1, dahinter die Liste der Partitionen */
    if (!block_read(dev, 1, 1, sector) || memcmp(sector, "EFI PART", 8) != 0)
        return false;
    uint64_t table = rd32(sector + 72) | ((uint64_t)rd32(sector + 76) << 32);
    uint32_t count = rd32(sector + 80);
    uint32_t entry_size = rd32(sector + 84);
    if (entry_size < 128 || entry_size > SECTOR_SIZE)
        return false;
    if (count > 128)
        count = 128;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t offset = i * entry_size;
        if (offset % SECTOR_SIZE == 0 &&
            !block_read(dev, table + offset / SECTOR_SIZE, 1, sector))
            return false;
        const uint8_t *entry = sector + offset % SECTOR_SIZE;
        if (offset % SECTOR_SIZE + entry_size > SECTOR_SIZE)
            continue;                   /* Eintrag über Sektorgrenze: überspringen */
        uint64_t first = rd32(entry + 32) | ((uint64_t)rd32(entry + 36) << 32);
        if (first == 0)
            continue;

        if (try_volume(fs, dev, first))
            return true;
    }
    return false;
}

void fat_unmount(fat_t *fs)
{
    kfree(fs->cache);
    fs->cache = NULL;
}

/* --- Zuordnungstabelle (FAT) ----------------------------------------------------------- */

static bool cache_flush(fat_t *fs)
{
    if (!fs->cache_dirty)
        return true;
    /* Jede Kopie der Tabelle bekommt dieselben Daten. */
    for (uint32_t copy = 0; copy < fs->fat_count; copy++) {
        uint64_t lba = fs->start + fs->fat_start + copy * fs->fat_sectors +
                       fs->cache_sector;
        if (!block_write(fs->dev, lba, fs->cache_sectors, fs->cache))
            return false;
    }
    fs->cache_dirty = false;
    return true;
}

/* Liefert einen Zeiger auf den Tabelleneintrag des Clusters (oder NULL). */
static uint8_t *fat_entry(fat_t *fs, uint32_t cluster)
{
    uint32_t sector = cluster / (SECTOR_SIZE / 4);
    if (sector >= fs->fat_sectors)
        return NULL;

    bool cached = fs->cache_sectors > 0 && sector >= fs->cache_sector &&
                  sector < fs->cache_sector + fs->cache_sectors;
    if (!cached) {
        if (!cache_flush(fs))
            return NULL;
        uint32_t first = sector - sector % CACHE_SECTORS;
        uint32_t count = fs->fat_sectors - first;
        if (count > CACHE_SECTORS)
            count = CACHE_SECTORS;
        fs->cache_sectors = 0;
        if (!block_read(fs->dev, fs->start + fs->fat_start + first, count,
                        fs->cache))
            return NULL;
        fs->cache_sector = first;
        fs->cache_sectors = count;
    }
    return fs->cache + (sector - fs->cache_sector) * SECTOR_SIZE +
           (cluster % (SECTOR_SIZE / 4)) * 4;
}

static bool valid_cluster(const fat_t *fs, uint32_t cluster)
{
    return cluster >= 2 && cluster < fs->cluster_count + 2;
}

/* Nächster Cluster der Kette; 0 am Ende oder bei einem Fehler. */
static uint32_t fat_next(fat_t *fs, uint32_t cluster)
{
    uint8_t *entry = fat_entry(fs, cluster);
    if (!entry)
        return 0;
    uint32_t next = rd32(entry) & FAT_MASK;
    return valid_cluster(fs, next) ? next : 0;
}

static bool fat_set(fat_t *fs, uint32_t cluster, uint32_t value)
{
    uint8_t *entry = fat_entry(fs, cluster);
    if (!entry)
        return false;
    /* Die obersten vier Bits gehören nicht zum Wert und bleiben erhalten. */
    wr32(entry, (rd32(entry) & ~FAT_MASK) | (value & FAT_MASK));
    fs->cache_dirty = true;
    return true;
}

static uint64_t cluster_lba(const fat_t *fs, uint32_t cluster)
{
    return fs->start + fs->data_start +
           (uint64_t)(cluster - 2) * fs->sectors_per_cluster;
}

/* --- Verzeichnisse ---------------------------------------------------------------------- */

/* Wandelt "bootx64.efi" in die gespeicherte Form "BOOTX64 EFI" (8 + 3 Zeichen). */
static bool short_name(const char *name, size_t len, uint8_t out[11])
{
    size_t pos = 0;
    memset(out, ' ', 11);
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (c == '.') {
            if (pos > 8)
                return false;           /* zweiter Punkt */
            pos = 8;
            continue;
        }
        if (pos == 8 && (i == 0 || name[i - 1] != '.'))
            return false;               /* Name länger als acht Zeichen */
        if (pos >= 11)
            return false;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        out[pos++] = (uint8_t)c;
    }
    return pos > 0;
}

static bool find_in_directory(fat_t *fs, uint32_t cluster,
                              const uint8_t name[11], fat_file_t *out)
{
    uint8_t *buffer = kmalloc(fs->sectors_per_cluster * SECTOR_SIZE);
    bool found = false, end = false;
    if (!buffer)
        return false;

    /* Schutz gegen kaputte Ketten, die im Kreis laufen */
    for (uint32_t guard = 0; cluster && !found && !end &&
                             guard < fs->cluster_count; guard++) {
        uint64_t lba = cluster_lba(fs, cluster);
        if (!block_read(fs->dev, lba, fs->sectors_per_cluster, buffer))
            break;
        uint32_t bytes = fs->sectors_per_cluster * SECTOR_SIZE;
        for (uint32_t offset = 0; offset < bytes; offset += 32) {
            const uint8_t *entry = buffer + offset;
            if (entry[0] == 0x00) {     /* ab hier ist das Verzeichnis leer */
                end = true;
                break;
            }
            if (entry[0] == 0xE5 || entry[11] == ATTR_LONG_NAME ||
                (entry[11] & ATTR_LABEL))
                continue;               /* gelöscht, langer Name, Datenträgername */
            if (memcmp(entry, name, 11) != 0)
                continue;
            out->first_cluster = ((uint32_t)rd16(entry + 20) << 16) |
                                 rd16(entry + 26);
            out->size = rd32(entry + 28);
            out->directory = entry[11] & ATTR_DIRECTORY;
            out->entry_sector = lba + offset / SECTOR_SIZE;
            out->entry_offset = offset % SECTOR_SIZE;
            found = true;
            break;
        }
        cluster = fat_next(fs, cluster);
    }
    kfree(buffer);
    return found;
}

bool fat_find(fat_t *fs, const char *path, fat_file_t *out)
{
    fat_file_t current = { .first_cluster = fs->root_cluster, .directory = true };

    while (*path) {
        size_t len = 0;
        while (path[len] && path[len] != '/')
            len++;
        uint8_t name[11];
        if (len == 0 || !current.directory || !short_name(path, len, name))
            return false;
        uint32_t directory = current.first_cluster;
        if (!valid_cluster(fs, directory) ||
            !find_in_directory(fs, directory, name, &current))
            return false;
        path += len;
        while (*path == '/')
            path++;
    }
    *out = current;
    return true;
}

/* --- Dateien lesen und ersetzen ----------------------------------------------------------- */

bool fat_read(fat_t *fs, const fat_file_t *file, void *buffer)
{
    uint32_t per_cluster = fs->sectors_per_cluster;
    uint32_t cluster_bytes = per_cluster * SECTOR_SIZE;
    uint8_t *scratch = kmalloc(cluster_bytes);
    uint8_t *out = buffer;
    uint32_t left = file->size;
    uint32_t cluster = file->first_cluster;
    bool ok = scratch != NULL;

    while (ok && left > 0) {
        if (!valid_cluster(fs, cluster)) {
            ok = false;
            break;
        }
        /* Aufeinanderfolgende Cluster in einem Rutsch lesen */
        uint32_t run = 1;
        uint32_t next = fat_next(fs, cluster);
        while (next == cluster + run && (run + 1) * per_cluster <= RUN_SECTORS &&
               (uint64_t)(run + 1) * cluster_bytes <= left) {
            run++;
            next = fat_next(fs, cluster + run - 1);
        }

        uint32_t bytes = run * cluster_bytes;
        if (bytes <= left) {
            ok = block_read(fs->dev, cluster_lba(fs, cluster), run * per_cluster,
                            out);
            out += bytes;
            left -= bytes;
        } else {                        /* letzter, nur teilweise belegter Cluster */
            ok = block_read(fs->dev, cluster_lba(fs, cluster), per_cluster,
                            scratch);
            memcpy(out, scratch, left);
            left = 0;
        }
        cluster = next;
    }
    kfree(scratch);
    return ok;
}

/* Gibt alle Cluster einer Kette frei. */
static bool free_chain(fat_t *fs, uint32_t cluster)
{
    for (uint32_t guard = 0; valid_cluster(fs, cluster) &&
                             guard < fs->cluster_count; guard++) {
        uint32_t next = fat_next(fs, cluster);
        if (!fat_set(fs, cluster, 0))
            return false;
        cluster = next;
    }
    return cache_flush(fs);
}

bool fat_replace(fat_t *fs, fat_file_t *file, const void *data, uint32_t size)
{
    uint32_t cluster_bytes = fs->sectors_per_cluster * SECTOR_SIZE;
    uint32_t needed = (size + cluster_bytes - 1) / cluster_bytes;
    if (needed == 0 || file->directory)
        return false;

    uint32_t *chain = kmalloc((size_t)needed * sizeof(uint32_t));
    uint8_t *scratch = kzalloc(cluster_bytes);
    bool ok = false;
    if (!chain || !scratch)
        goto done;

    /* 1. Genügend freie Cluster suchen */
    uint32_t found = 0;
    for (uint32_t c = 2; c < fs->cluster_count + 2 && found < needed; c++) {
        uint8_t *entry = fat_entry(fs, c);
        if (!entry)
            goto done;
        if ((rd32(entry) & FAT_MASK) == 0)
            chain[found++] = c;
    }
    if (found < needed)
        goto done;                      /* Datenträger voll */

    /* 2. Den neuen Inhalt dorthin schreiben - die alte Datei bleibt unberührt */
    const uint8_t *in = data;
    uint32_t left = size;
    uint32_t per_cluster = fs->sectors_per_cluster;
    for (uint32_t i = 0; i < needed; ) {
        /* Aufeinanderfolgende Cluster in einem Rutsch schreiben */
        uint32_t run = 1;
        while (i + run < needed && chain[i + run] == chain[i] + run &&
               (run + 1) * per_cluster <= RUN_SECTORS &&
               (uint64_t)(run + 1) * cluster_bytes <= left)
            run++;

        uint32_t bytes = run * cluster_bytes;
        if (bytes <= left) {
            if (!block_write(fs->dev, cluster_lba(fs, chain[i]),
                             run * per_cluster, in))
                goto done;
            in += bytes;
            left -= bytes;
        } else {                        /* letzter Cluster: mit Nullen auffüllen */
            memcpy(scratch, in, left);
            if (!block_write(fs->dev, cluster_lba(fs, chain[i]), per_cluster,
                             scratch))
                goto done;
            left = 0;
        }
        i += run;
    }

    /* 3. Die neuen Cluster in der Tabelle zu einer Kette verbinden */
    for (uint32_t i = 0; i < needed; i++) {
        uint32_t next = i + 1 < needed ? chain[i + 1] : FAT_MASK;
        if (!fat_set(fs, chain[i], next))
            goto done;
    }
    if (!cache_flush(fs))
        goto done;

    /* 4. Der entscheidende Schritt: Verzeichniseintrag auf die neue Kette */
    uint8_t sector[SECTOR_SIZE];
    if (!block_read(fs->dev, file->entry_sector, 1, sector))
        goto done;
    uint8_t *entry = sector + file->entry_offset;
    wr16(entry + 20, (uint16_t)(chain[0] >> 16));
    wr16(entry + 26, (uint16_t)chain[0]);
    wr32(entry + 28, size);
    if (!block_write(fs->dev, file->entry_sector, 1, sector))
        goto done;

    /* 5. Aufräumen: alte Cluster freigeben, Zähler im Infosektor verwerfen */
    uint32_t old = file->first_cluster;
    file->first_cluster = chain[0];
    file->size = size;
    ok = true;
    free_chain(fs, old);
    if (fs->info_sector && block_read(fs->dev, fs->start + fs->info_sector, 1, sector) &&
        rd32(sector) == 0x41615252 && rd32(sector + 484) == 0x61417272) {
        wr32(sector + 488, 0xFFFFFFFF); /* "Anzahl freier Cluster unbekannt" */
        wr32(sector + 492, 0xFFFFFFFF);
        block_write(fs->dev, fs->start + fs->info_sector, 1, sector);
    }

done:
    if (!ok)
        fs->cache_sectors = 0;          /* halbfertige Änderungen verwerfen */
    fs->cache_dirty = ok && fs->cache_dirty;
    kfree(chain);
    kfree(scratch);
    return ok;
}
