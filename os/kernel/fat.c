#include "fat.h"
#include "console.h"
#include "memory.h"
#include "string.h"
#include "time.h"

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
    fs->sectors = total;
    if (start > dev->sectors || total > dev->sectors - start)
        return false;                   /* reicht über den Datenträger hinaus */

    /* Die FAT muss für alle Cluster einen Eintrag haben. */
    if ((uint64_t)(fs->cluster_count + 2) * 4 >
        (uint64_t)fat_sectors * SECTOR_SIZE)
        return false;
    fs->cache = kmalloc(CACHE_SECTORS * SECTOR_SIZE);
    return fs->cache != NULL;
}

/* Zählt Fundstellen herunter, bis die gewünschte erreicht ist. */
static bool nth_volume(fat_t *fs, block_device_t *dev, uint64_t start, int *skip)
{
    if (!try_volume(fs, dev, start))
        return false;
    if (*skip == 0)
        return true;
    (*skip)--;
    fat_unmount(fs);
    return false;
}

bool fat_mount_nth(fat_t *fs, block_device_t *dev, int n)
{
    uint8_t sector[SECTOR_SIZE];
    memset(fs, 0, sizeof(*fs));

    /* 1. Datenträger ohne Partitionstabelle */
    if (try_volume(fs, dev, 0))
        return n == 0 ? true : (fat_unmount(fs), false);
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
        else if (entry[4] != 0 && first != 0 && nth_volume(fs, dev, first, &n))
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

        if (nth_volume(fs, dev, first, &n))
            return true;
    }
    return false;
}

bool fat_mount(fat_t *fs, block_device_t *dev)
{
    return fat_mount_nth(fs, dev, 0);
}

bool fat_mount_at(fat_t *fs, block_device_t *dev, uint64_t start)
{
    memset(fs, 0, sizeof(*fs));
    return try_volume(fs, dev, start);
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

/* Trägt Datum und Uhrzeit der letzten Änderung in einen Verzeichniseintrag ein. */
static void stamp(uint8_t *entry)
{
    datetime_t now;
    time_now(&now);
    if (now.year < 1980 || now.year > 2107)
        return;
    wr16(entry + 22, (uint16_t)((now.hour << 11) | (now.minute << 5) |
                                (now.second / 2)));
    wr16(entry + 24, (uint16_t)(((now.year - 1980) << 9) | (now.month << 5) |
                                now.day));
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

/* Der Infosektor merkt sich die Zahl freier Cluster - nach Änderungen stimmt sie nicht mehr. */
static void forget_free_count(fat_t *fs)
{
    uint8_t sector[SECTOR_SIZE];
    if (fs->info_sector && block_read(fs->dev, fs->start + fs->info_sector, 1, sector) &&
        rd32(sector) == 0x41615252 && rd32(sector + 484) == 0x61417272 &&
        rd32(sector + 488) != 0xFFFFFFFF) {
        wr32(sector + 488, 0xFFFFFFFF); /* "Anzahl freier Cluster unbekannt" */
        wr32(sector + 492, 0xFFFFFFFF);
        block_write(fs->dev, fs->start + fs->info_sector, 1, sector);
    }
}

bool fat_replace(fat_t *fs, fat_file_t *file, const void *data, uint32_t size)
{
    uint32_t cluster_bytes = fs->sectors_per_cluster * SECTOR_SIZE;
    uint32_t needed = (size + cluster_bytes - 1) / cluster_bytes;
    if (file->directory)
        return false;

    /* Eine leere Datei belegt keinen Cluster; chain[0] ist dann 0. */
    uint32_t *chain = kzalloc((size_t)(needed + 1) * sizeof(uint32_t));
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
    stamp(entry);
    if (!block_write(fs->dev, file->entry_sector, 1, sector))
        goto done;

    /* 5. Aufräumen: alte Cluster freigeben, Zähler im Infosektor verwerfen */
    uint32_t old = file->first_cluster;
    file->first_cluster = chain[0];
    file->size = size;
    ok = true;
    free_chain(fs, old);
    forget_free_count(fs);

done:
    if (!ok)
        fs->cache_sectors = 0;          /* halbfertige Änderungen verwerfen */
    fs->cache_dirty = ok && fs->cache_dirty;
    kfree(chain);
    kfree(scratch);
    return ok;
}

/* --- Eigene Dateien: Verzeichnisse als Ganzes bearbeiten ----------------------------------- */

#define DIR_BYTES_MAX  (1024 * 1024)    /* größer wird kein Verzeichnis geladen */
#define LFN_CHARS      13               /* Zeichen pro Eintrag eines langen Namens */
#define LFN_ENTRIES    20               /* 255 Zeichen */

/* Ein vollständig in den Speicher geladenes Verzeichnis */
typedef struct {
    uint8_t *data;
    uint32_t bytes;
    uint32_t *clusters;
    uint32_t count;
} dir_t;

static void dir_free(dir_t *d)
{
    kfree(d->data);
    kfree(d->clusters);
    memset(d, 0, sizeof(*d));
}

static bool dir_load(fat_t *fs, uint32_t first, dir_t *d)
{
    uint32_t cluster_bytes = fs->sectors_per_cluster * SECTOR_SIZE;
    uint32_t max = DIR_BYTES_MAX / cluster_bytes + 1;
    uint32_t count = 0;

    memset(d, 0, sizeof(*d));
    for (uint32_t c = first; valid_cluster(fs, c); c = fat_next(fs, c)) {
        if (++count > max)
            return false;               /* zu groß oder im Kreis verkettet */
    }
    if (count == 0)
        return false;

    d->clusters = kmalloc(count * sizeof(uint32_t));
    d->data = kmalloc((size_t)count * cluster_bytes);
    if (!d->clusters || !d->data) {
        dir_free(d);
        return false;
    }
    uint32_t c = first;
    for (uint32_t i = 0; i < count; i++, c = fat_next(fs, c)) {
        d->clusters[i] = c;
        if (!block_read(fs->dev, cluster_lba(fs, c), fs->sectors_per_cluster,
                        d->data + (size_t)i * cluster_bytes)) {
            dir_free(d);
            return false;
        }
    }
    d->count = count;
    d->bytes = count * cluster_bytes;
    return true;
}

/* Schreibt die Sektoren zurück, in denen der Bereich [offset, offset+len) liegt. */
static bool dir_write(fat_t *fs, const dir_t *d, uint32_t offset, uint32_t len)
{
    uint32_t cluster_bytes = fs->sectors_per_cluster * SECTOR_SIZE;
    uint32_t first = offset / SECTOR_SIZE;
    uint32_t last = (offset + len - 1) / SECTOR_SIZE;

    for (uint32_t sector = first; sector <= last; sector++) {
        uint32_t position = sector * SECTOR_SIZE;
        uint64_t lba = cluster_lba(fs, d->clusters[position / cluster_bytes]) +
                       (position % cluster_bytes) / SECTOR_SIZE;
        if (!block_write(fs->dev, lba, 1, d->data + position))
            return false;
    }
    return true;
}

static uint64_t dir_entry_sector(const fat_t *fs, const dir_t *d, uint32_t offset)
{
    uint32_t cluster_bytes = fs->sectors_per_cluster * SECTOR_SIZE;
    return cluster_lba(fs, d->clusters[offset / cluster_bytes]) +
           (offset % cluster_bytes) / SECTOR_SIZE;
}

/* Sucht einen freien Cluster, füllt ihn mit Nullen und markiert ihn als Kettenende. */
static uint32_t allocate_cluster(fat_t *fs)
{
    uint32_t cluster_bytes = fs->sectors_per_cluster * SECTOR_SIZE;
    uint32_t found = 0;

    for (uint32_t c = 2; c < fs->cluster_count + 2 && !found; c++) {
        uint8_t *entry = fat_entry(fs, c);
        if (!entry)
            return 0;
        if ((rd32(entry) & FAT_MASK) == 0)
            found = c;
    }
    uint8_t *zero = found ? kzalloc(cluster_bytes) : NULL;
    if (!zero)
        return 0;
    bool ok = block_write(fs->dev, cluster_lba(fs, found),
                          fs->sectors_per_cluster, zero) &&
              fat_set(fs, found, FAT_MASK) && cache_flush(fs);
    kfree(zero);
    return ok ? found : 0;
}

/* Hängt einen leeren Cluster an das Verzeichnis an. */
static bool dir_grow(fat_t *fs, dir_t *d)
{
    uint32_t cluster_bytes = fs->sectors_per_cluster * SECTOR_SIZE;
    if (d->bytes + cluster_bytes > DIR_BYTES_MAX)
        return false;

    uint8_t *data = kzalloc(d->bytes + cluster_bytes);
    uint32_t *clusters = kmalloc((d->count + 1) * sizeof(uint32_t));
    uint32_t added = (data && clusters) ? allocate_cluster(fs) : 0;
    if (!added || !fat_set(fs, d->clusters[d->count - 1], added) ||
        !cache_flush(fs)) {
        kfree(data);
        kfree(clusters);
        return false;
    }
    memcpy(data, d->data, d->bytes);
    memcpy(clusters, d->clusters, d->count * sizeof(uint32_t));
    clusters[d->count] = added;
    kfree(d->data);
    kfree(d->clusters);
    d->data = data;
    d->clusters = clusters;
    d->count++;
    d->bytes += cluster_bytes;
    return true;
}

/* Prüfsumme des kurzen Namens; verbindet ihn mit den Einträgen des langen. */
static uint8_t lfn_checksum(const uint8_t name[11])
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + name[i]);
    return sum;
}

static const uint8_t lfn_positions[LFN_CHARS] = {
    1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
};

static size_t utf8_put(char *out, uint32_t cp)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

/* Liest ein UTF-8-Zeichen; alles außerhalb der Grundebene wird zu '?'. */
static uint32_t utf8_get(const char **text)
{
    const uint8_t *p = (const uint8_t *)*text;
    uint32_t cp = *p++;
    int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;

    if (cp >= 0x80)
        cp = extra ? cp & (0x3F >> extra) : '?';
    while (extra-- > 0 && (*p & 0xC0) == 0x80)
        cp = (cp << 6) | (*p++ & 0x3F);
    *text = (const char *)p;
    return cp > 0xFFFF ? '?' : cp;
}

static uint32_t fold(uint32_t cp)
{
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7))
        return cp + 32;
    return cp;
}

/* Vergleicht zwei Namen ohne Rücksicht auf Groß- und Kleinschreibung. */
bool fat_same_name(const char *a, const char *b)
{
    for (;;) {
        uint32_t x = fold(utf8_get(&a)), y = fold(utf8_get(&b));
        if (x != y)
            return false;
        if (x == 0)
            return true;
    }
}

/* "NOTIZ   TXT" -> "notiz.txt" bzw. "NOTIZ.TXT", je nach Kennzeichen in flags. */
static void short_to_text(const uint8_t *entry, char *out)
{
    size_t n = 0;
    for (int i = 0; i < 11; i++) {
        uint8_t c = entry[i];
        if (i == 0 && c == 0x05)
            c = 0xE5;
        if (c == ' ')
            continue;
        if (i == 8)
            out[n++] = '.';
        bool lower = entry[12] & (i < 8 ? 0x08 : 0x10);
        if (lower && c >= 'A' && c <= 'Z')
            c = (uint8_t)(c + 32);
        if (c >= 0x80)
            c = '_';                    /* Zeichen der alten DOS-Codeseiten */
        out[n++] = (char)c;
    }
    out[n] = '\0';
}

static void fill_file(const fat_t *fs, const dir_t *d, uint32_t first,
                      uint32_t offset, uint32_t name_entries, fat_file_t *file)
{
    const uint8_t *entry = d->data + offset;
    file->first_cluster = ((uint32_t)rd16(entry + 20) << 16) | rd16(entry + 26);
    file->size = rd32(entry + 28);
    file->directory = entry[11] & ATTR_DIRECTORY;
    file->entry_sector = dir_entry_sector(fs, d, offset);
    file->entry_offset = offset % SECTOR_SIZE;
    file->dir_cluster = first;
    file->dir_offset = offset;
    file->name_entries = name_entries;
}

/* Geht alle Einträge eines geladenen Verzeichnisses durch. */
static void dir_walk(const fat_t *fs, const dir_t *d, uint32_t first,
                     fat_visit_t visit, void *ctx)
{
    uint16_t wide[LFN_ENTRIES * LFN_CHARS + 1];
    uint32_t expected = 0, total = 0;   /* Zustand des langen Namens davor */
    uint8_t checksum = 0;
    char name[FAT_NAME_MAX * 3];

    for (uint32_t offset = 0; offset + 32 <= d->bytes; offset += 32) {
        const uint8_t *entry = d->data + offset;
        if (entry[0] == 0x00)
            break;
        if (entry[0] == 0xE5) {
            total = 0;
            continue;
        }
        if (entry[11] == ATTR_LONG_NAME) {
            uint32_t seq = entry[0] & 0x3F;
            if (entry[0] & 0x40) {      /* letzter Teil steht zuerst */
                total = expected = seq;
                checksum = entry[13];
                memset(wide, 0, sizeof(wide));
            }
            if (seq == 0 || seq > LFN_ENTRIES || seq != expected ||
                entry[13] != checksum) {
                total = 0;
                expected = 0;
                continue;
            }
            for (int i = 0; i < LFN_CHARS; i++)
                wide[(seq - 1) * LFN_CHARS + i] = rd16(entry + lfn_positions[i]);
            expected--;
            continue;
        }
        bool has_long = total > 0 && expected == 0 &&
                        lfn_checksum(entry) == checksum;
        uint32_t name_entries = has_long ? total : 0;
        total = 0;
        if (entry[11] & ATTR_LABEL)
            continue;
        if (entry[0] == '.')
            continue;                   /* "." und ".." */

        if (has_long) {
            size_t n = 0;
            for (int i = 0; wide[i] && wide[i] != 0xFFFF; i++) {
                uint32_t cp = wide[i];
                if (cp >= 0xD800 && cp <= 0xDFFF)
                    cp = '?';
                n += utf8_put(name + n, cp);
            }
            name[n] = '\0';
        } else {
            short_to_text(entry, name);
        }
        if (strlen(name) >= FAT_NAME_MAX)
            continue;

        fat_file_t file;
        fill_file(fs, d, first, offset, name_entries, &file);
        if (!visit(ctx, name, &file))
            break;
    }
}

bool fat_list(fat_t *fs, uint32_t dir_cluster, fat_visit_t visit, void *ctx)
{
    dir_t d;
    if (!dir_load(fs, dir_cluster, &d))
        return false;
    dir_walk(fs, &d, dir_cluster, visit, ctx);
    dir_free(&d);
    return true;
}

typedef struct {
    const char *name;
    fat_file_t *out;
    bool found;
} lookup_t;

static bool lookup_visit(void *ctx, const char *name, const fat_file_t *file)
{
    lookup_t *l = ctx;
    if (!fat_same_name(name, l->name))
        return true;
    *l->out = *file;
    l->found = true;
    return false;
}

bool fat_lookup(fat_t *fs, uint32_t dir_cluster, const char *name, fat_file_t *out)
{
    lookup_t l = { name, out, false };
    return fat_list(fs, dir_cluster, lookup_visit, &l) && l.found;
}

/* Gibt es im Verzeichnis schon einen Eintrag mit diesem kurzen Namen? */
static bool short_exists(const dir_t *d, const uint8_t name[11])
{
    for (uint32_t offset = 0; offset + 32 <= d->bytes; offset += 32) {
        const uint8_t *entry = d->data + offset;
        if (entry[0] == 0x00)
            break;
        if (entry[0] != 0xE5 && entry[11] != ATTR_LONG_NAME &&
            memcmp(entry, name, 11) == 0)
            return true;
    }
    return false;
}

static uint8_t short_char(uint32_t cp)
{
    if (cp >= 'a' && cp <= 'z')
        return (uint8_t)(cp - 32);
    if ((cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || cp == '_' ||
        cp == '-')
        return (uint8_t)cp;
    return '_';
}

/* Erfindet zum langen Namen einen freien kurzen: "BRIEFA~1TXT". */
static bool make_alias(const dir_t *d, const char *name, uint8_t out[11])
{
    uint8_t base[6], ext[3];
    int base_len = 0, ext_len = 0;
    const char *dot = NULL;

    for (const char *p = name; *p; p++) {
        if (*p == '.' && p != name)
            dot = p;
    }
    for (const char *p = name; *p && p != dot && base_len < 6; ) {
        uint32_t cp = utf8_get(&p);
        if (cp != ' ' && cp != '.')
            base[base_len++] = short_char(cp);
    }
    for (const char *p = dot ? dot + 1 : ""; *p && ext_len < 3; ) {
        uint32_t cp = utf8_get(&p);
        if (cp != ' ')
            ext[ext_len++] = short_char(cp);
    }
    if (base_len == 0)
        base[base_len++] = '_';

    for (uint32_t n = 1; n < 100000; n++) {
        char tail[8];
        int tail_len = ksnprintf(tail, sizeof(tail), "~%u", n);
        int keep = base_len < 8 - tail_len ? base_len : 8 - tail_len;
        memset(out, ' ', 11);
        memcpy(out, base, (size_t)keep);
        memcpy(out + keep, tail, (size_t)tail_len);
        memcpy(out + 8, ext, (size_t)ext_len);
        if (!short_exists(d, out))
            return true;
    }
    return false;
}

bool fat_create(fat_t *fs, uint32_t dir_cluster, const char *name, bool directory,
                fat_file_t *out)
{
    uint16_t wide[LFN_ENTRIES * LFN_CHARS];
    uint32_t chars = 0;
    for (const char *p = name; *p; ) {
        if (chars >= 255)
            return false;
        wide[chars++] = (uint16_t)utf8_get(&p);
    }
    if (chars == 0)
        return false;
    uint32_t name_entries = (chars + LFN_CHARS - 1) / LFN_CHARS;
    uint32_t needed = name_entries + 1;

    dir_t d;
    bool ok = false;
    uint32_t content = 0;
    if (!dir_load(fs, dir_cluster, &d))
        return false;

    /* Eine Lücke aus genügend freien Einträgen suchen */
    uint32_t start = 0, run = 0;
    for (;;) {
        run = 0;
        for (uint32_t offset = 0; offset + 32 <= d.bytes && run < needed;
             offset += 32) {
            uint8_t first = d.data[offset];
            if (first == 0x00) {        /* ab hier ist alles frei */
                if (run == 0)
                    start = offset;
                run += (d.bytes - offset) / 32;
                break;
            }
            if (first == 0xE5) {
                if (run++ == 0)
                    start = offset;
            } else {
                run = 0;
            }
        }
        if (run >= needed)
            break;
        if (!dir_grow(fs, &d))
            goto done;
    }

    uint8_t alias[11];
    if (!make_alias(&d, name, alias))
        goto done;

    if (directory) {                    /* ein Cluster mit "." und ".." */
        content = allocate_cluster(fs);
        uint8_t *block = content ? kzalloc(SECTOR_SIZE) : NULL;
        if (!block)
            goto done;
        uint32_t parent = dir_cluster == fs->root_cluster ? 0 : dir_cluster;
        memset(block, ' ', 11);
        memset(block + 32, ' ', 11);
        block[0] = '.';
        block[32] = block[33] = '.';
        block[11] = block[32 + 11] = ATTR_DIRECTORY;
        wr16(block + 20, (uint16_t)(content >> 16));
        wr16(block + 26, (uint16_t)content);
        wr16(block + 32 + 20, (uint16_t)(parent >> 16));
        wr16(block + 32 + 26, (uint16_t)parent);
        stamp(block);
        stamp(block + 32);
        bool written = block_write(fs->dev, cluster_lba(fs, content), 1, block);
        kfree(block);
        if (!written)
            goto done;
    }

    /* Die Einträge des langen Namens stehen rückwärts vor dem eigentlichen. */
    uint8_t sum = lfn_checksum(alias);
    for (uint32_t i = 0; i < name_entries; i++) {
        uint32_t seq = name_entries - i;
        uint8_t *entry = d.data + start + i * 32;
        memset(entry, 0, 32);
        entry[0] = (uint8_t)(seq | (i == 0 ? 0x40 : 0));
        entry[11] = ATTR_LONG_NAME;
        entry[13] = sum;
        for (uint32_t c = 0; c < LFN_CHARS; c++) {
            uint32_t index = (seq - 1) * LFN_CHARS + c;
            uint16_t value = index < chars ? wide[index]
                           : index == chars ? 0 : 0xFFFF;
            wr16(entry + lfn_positions[c], value);
        }
    }
    uint32_t offset = start + name_entries * 32;
    uint8_t *entry = d.data + offset;
    memset(entry, 0, 32);
    memcpy(entry, alias, 11);
    entry[11] = directory ? ATTR_DIRECTORY : 0x20;      /* 0x20: "Archiv" */
    wr16(entry + 20, (uint16_t)(content >> 16));
    wr16(entry + 26, (uint16_t)content);
    stamp(entry);
    wr16(entry + 14, rd16(entry + 22));                 /* angelegt = geändert */
    wr16(entry + 16, rd16(entry + 24));
    if (!dir_write(fs, &d, start, needed * 32))
        goto done;

    fill_file(fs, &d, dir_cluster, offset, name_entries, out);
    forget_free_count(fs);
    ok = true;

done:
    if (!ok && content)
        free_chain(fs, content);
    dir_free(&d);
    return ok;
}

bool fat_delete(fat_t *fs, const fat_file_t *file)
{
    dir_t d;
    if (file->directory || !dir_load(fs, file->dir_cluster, &d))
        return false;

    uint32_t start = file->dir_offset - file->name_entries * 32;
    bool ok = file->dir_offset + 32 <= d.bytes &&
              file->name_entries * 32 <= file->dir_offset;
    if (ok) {
        for (uint32_t offset = start; offset <= file->dir_offset; offset += 32)
            d.data[offset] = 0xE5;
        ok = dir_write(fs, &d, start, file->dir_offset + 32 - start);
    }
    dir_free(&d);
    if (ok) {
        free_chain(fs, file->first_cluster);
        forget_free_count(fs);
    }
    return ok;
}

void fat_label(fat_t *fs, char out[12])
{
    uint8_t name[11];
    bool found = false;
    dir_t d;

    /* Maßgeblich ist der Eintrag im Wurzelverzeichnis - den ändert Windows beim Umbenennen. */
    if (dir_load(fs, fs->root_cluster, &d)) {
        for (uint32_t offset = 0; offset + 32 <= d.bytes && !found; offset += 32) {
            const uint8_t *entry = d.data + offset;
            if (entry[0] == 0x00)
                break;
            if (entry[0] != 0xE5 && entry[11] != ATTR_LONG_NAME &&
                (entry[11] & ATTR_LABEL)) {
                memcpy(name, entry, 11);
                found = true;
            }
        }
        dir_free(&d);
    }
    if (!found) {                       /* sonst das Feld im Startsektor */
        uint8_t boot[SECTOR_SIZE];
        if (block_read(fs->dev, fs->start, 1, boot)) {
            memcpy(name, boot + 71, 11);
            found = true;
        }
    }
    size_t n = 0;
    for (int i = 0; found && i < 11; i++) {
        uint8_t c = name[i];
        out[n++] = (c >= 32 && c < 127) ? (char)c : '?';
    }
    while (n > 0 && out[n - 1] == ' ')
        n--;
    out[n] = '\0';
}
