/*
 * NVMe: SSDs, die direkt am PCI-Express-Bus hängen.
 *
 * Der Rechner legt Aufträge (64 Bytes) in eine Warteschlange im Speicher und
 * meldet dem Controller über ein "Doorbell"-Register, dass etwas Neues da
 * ist; der Controller schreibt das Ergebnis (16 Bytes) in eine zweite
 * Warteschlange. Es gibt ein solches Paar für die Verwaltung und eines für
 * Lese- und Schreibaufträge.
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

#define CONTROLLERS_MAX 2
#define QUEUE_ENTRIES   32
#define PAGE            4096
#define CHUNK_PAGES     8               /* höchstens 32 KiB je Auftrag */

/* Register des Controllers */
#define REG_CAP   0x00
#define REG_CC    0x14
#define REG_CSTS  0x1C
#define REG_AQA   0x24
#define REG_ASQ   0x28
#define REG_ACQ   0x30
#define CC_ENABLE 0x00000001u
#define CSTS_READY 0x1u
#define CSTS_FATAL 0x2u

/* Verwaltungsaufträge */
#define ADMIN_CREATE_SQ 0x01
#define ADMIN_CREATE_CQ 0x05
#define ADMIN_IDENTIFY  0x06
#define ADMIN_FEATURES  0x09
/* Lese- und Schreibaufträge */
#define IO_FLUSH 0x00
#define IO_WRITE 0x01
#define IO_READ  0x02

typedef struct {
    uint32_t cdw0;                      /* Auftragsart und laufende Nummer */
    uint32_t nsid;
    uint64_t reserved;
    uint64_t metadata;
    uint64_t prp1, prp2;                /* Speicherseiten mit den Daten */
    uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
} command_t;

typedef struct {
    uint32_t result;
    uint32_t reserved;
    uint16_t sq_head, sq_id;
    uint16_t command_id;
    uint16_t status;                    /* Bit 0: Phase, darüber der Fehlercode */
} completion_t;

typedef struct {
    command_t *sq;
    volatile completion_t *cq;
    uint32_t tail, head;
    uint32_t phase;
    int id;
} queue_t;

typedef struct {
    volatile uint8_t *regs;
    uint32_t doorbell_stride;
    queue_t admin, io;
    uint32_t nsid;
    uint32_t chunk_sectors;
    uint8_t *buffer;                    /* Zwischenpuffer für die Daten */
    uint64_t *prp_list;
    uint16_t next_id;
    bool busy;
    bool dead;                          /* nach einem Fehler: nichts mehr anfassen */
    block_device_t *block;
} nvme_t;

static nvme_t controllers[CONTROLLERS_MAX];
static int controller_count;

static uint32_t read32(nvme_t *n, uint32_t reg)
{
    return *(volatile uint32_t *)(n->regs + reg);
}

static void write32(nvme_t *n, uint32_t reg, uint32_t value)
{
    *(volatile uint32_t *)(n->regs + reg) = value;
}

static uint64_t read64(nvme_t *n, uint32_t reg)
{
    return read32(n, reg) | ((uint64_t)read32(n, reg + 4) << 32);
}

static void write64(nvme_t *n, uint32_t reg, uint64_t value)
{
    write32(n, reg, (uint32_t)value);
    write32(n, reg + 4, (uint32_t)(value >> 32));
}

static void doorbell(nvme_t *n, int queue, bool completion, uint32_t value)
{
    write32(n, 0x1000 + (uint32_t)(2 * queue + (completion ? 1 : 0)) * n->doorbell_stride,
            value);
}

static bool wait_ready(nvme_t *n, bool ready, uint32_t timeout_ms)
{
    uint64_t deadline = time_ms() + timeout_ms;
    while (time_ms() < deadline) {
        uint32_t status = read32(n, REG_CSTS);
        if (status & CSTS_FATAL)
            return false;
        if (((status & CSTS_READY) != 0) == ready)
            return true;
    }
    return false;
}

/* Schickt einen Auftrag ab und wartet auf das Ergebnis. */
static bool submit(nvme_t *n, queue_t *q, command_t *command, uint32_t timeout_ms)
{
    if (n->dead)
        return false;
    uint16_t id = n->next_id++;
    command->cdw0 = (command->cdw0 & 0xFF) | ((uint32_t)id << 16);
    q->sq[q->tail] = *command;
    q->tail = (q->tail + 1) % QUEUE_ENTRIES;
    doorbell(n, q->id, false, q->tail);

    uint64_t deadline = time_ms() + timeout_ms;
    for (;;) {
        volatile completion_t *c = &q->cq[q->head];
        if ((c->status & 1) == q->phase) {
            uint16_t status = c->status >> 1;
            bool ours = c->command_id == id;
            q->head = (q->head + 1) % QUEUE_ENTRIES;
            if (q->head == 0)
                q->phase ^= 1;
            doorbell(n, q->id, true, q->head);
            if (ours)
                return status == 0;
            continue;                   /* Antwort auf etwas Älteres: weiter warten */
        }
        if (time_ms() >= deadline) {
            n->dead = true;             /* Zustand unklar - lieber nichts mehr schreiben */
            klog("NVMe: keine Antwort auf einen Auftrag - Datenträger wird nicht mehr benutzt\n");
            return false;
        }
        cpu_pause();
    }
}

/* Nur ein Task zur Zeit darf mit dem Controller sprechen. */
static void lock(nvme_t *n)
{
    for (;;) {
        uint64_t flags = irq_save();
        if (!n->busy) {
            n->busy = true;
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
    nvme_t *n = block->driver_data;
    uint8_t *data = buffer;
    bool ok = true;

    lock(n);
    while (ok && count > 0) {
        uint32_t sectors = count < n->chunk_sectors ? count : n->chunk_sectors;
        size_t bytes = (size_t)sectors * SECTOR_SIZE;
        if (write)
            memcpy(n->buffer, data, bytes);

        command_t c;
        memset(&c, 0, sizeof(c));
        c.cdw0 = write ? IO_WRITE : IO_READ;
        c.nsid = n->nsid;
        c.prp1 = (uint64_t)n->buffer;
        if (bytes > 2 * PAGE)
            c.prp2 = (uint64_t)n->prp_list;     /* Liste der weiteren Seiten */
        else if (bytes > PAGE)
            c.prp2 = (uint64_t)n->buffer + PAGE;
        c.cdw10 = (uint32_t)lba;
        c.cdw11 = (uint32_t)(lba >> 32);
        c.cdw12 = sectors - 1;
        ok = submit(n, &n->io, &c, 10000);
        if (ok && !write)
            memcpy(data, n->buffer, bytes);
        data += bytes;
        lba += sectors;
        count -= sectors;
    }
    n->busy = false;
    return ok;
}

static bool nvme_read(block_device_t *block, uint64_t lba, uint32_t count, void *buffer)
{
    return transfer(block, false, lba, count, buffer);
}

static bool nvme_write(block_device_t *block, uint64_t lba, uint32_t count,
                       const void *buffer)
{
    return transfer(block, true, lba, count, (void *)buffer);
}

static bool nvme_flush(block_device_t *block)
{
    nvme_t *n = block->driver_data;
    command_t c;
    memset(&c, 0, sizeof(c));
    c.cdw0 = IO_FLUSH;
    c.nsid = n->nsid;
    lock(n);
    bool ok = submit(n, &n->io, &c, 30000);
    n->busy = false;
    return ok;
}

static bool setup_queue(queue_t *q, int id)
{
    q->sq = alloc_pages(1);
    q->cq = alloc_pages(1);
    q->tail = q->head = 0;
    q->phase = 1;
    q->id = id;
    return q->sq && q->cq;
}

static bool start(nvme_t *n, const pci_device_t *pci, int index)
{
    uint64_t bar = (pci_read32(pci, 0x10) & ~0xFull) |
                   ((uint64_t)pci_read32(pci, 0x14) << 32);
    if (bar == 0)
        return false;
    /* Speicherzugriffe und eigene Buszugriffe erlauben, Interrupt-Leitung aus */
    pci_write16(pci, 0x04, (uint16_t)((pci_read16(pci, 0x04) | 0x0006 | 0x0400)));
    vmm_map(bar, 0x4000);
    n->regs = (volatile uint8_t *)bar;

    uint64_t cap = read64(n, REG_CAP);
    uint32_t timeout_ms = (uint32_t)((cap >> 24) & 0xFF) * 500 + 500;
    n->doorbell_stride = 4u << ((cap >> 32) & 0xF);
    if (((cap >> 48) & 0xF) != 0 || (cap & 0xFFFF) + 1 < QUEUE_ENTRIES) {
        klog("NVMe %d: Controller mit ungewöhnlichen Eigenschaften - übersprungen\n", index);
        return false;
    }

    /* Ausschalten, Verwaltungs-Warteschlangen einrichten, wieder einschalten */
    write32(n, REG_CC, read32(n, REG_CC) & ~CC_ENABLE);
    if (!wait_ready(n, false, timeout_ms) || !setup_queue(&n->admin, 0) ||
        !setup_queue(&n->io, 1))
        return false;
    n->buffer = alloc_pages(CHUNK_PAGES);
    n->prp_list = alloc_pages(1);
    uint8_t *info = alloc_pages(1);
    if (!n->buffer || !n->prp_list || !info)
        return false;
    for (int i = 1; i < CHUNK_PAGES; i++)
        n->prp_list[i - 1] = (uint64_t)n->buffer + (uint64_t)i * PAGE;

    write32(n, REG_AQA, ((QUEUE_ENTRIES - 1) << 16) | (QUEUE_ENTRIES - 1));
    write64(n, REG_ASQ, (uint64_t)n->admin.sq);
    write64(n, REG_ACQ, (uint64_t)n->admin.cq);
    /* Einträge: Aufträge 2^6, Ergebnisse 2^4 Bytes; Seitengröße 4 KiB */
    write32(n, REG_CC, (6u << 16) | (4u << 20) | CC_ENABLE);
    if (!wait_ready(n, true, timeout_ms))
        return false;

    /* Was für ein Laufwerk ist das? */
    command_t c;
    memset(&c, 0, sizeof(c));
    c.cdw0 = ADMIN_IDENTIFY;
    c.prp1 = (uint64_t)info;
    c.cdw10 = 1;                        /* Beschreibung des Controllers */
    if (!submit(n, &n->admin, &c, 5000))
        return false;
    char model[41];
    memcpy(model, info + 24, 40);
    model[40] = '\0';
    for (int i = 39; i >= 0 && model[i] == ' '; i--)
        model[i] = '\0';
    uint8_t mdts = info[77];            /* größter Auftrag: 2^mdts Seiten (0: beliebig) */
    uint32_t pages = CHUNK_PAGES;
    if (mdts && mdts < 3)
        pages = 1u << mdts;
    n->chunk_sectors = pages * PAGE / SECTOR_SIZE;

    /* Erster vorhandener Datenbereich ("Namespace") */
    memset(&c, 0, sizeof(c));
    c.cdw0 = ADMIN_IDENTIFY;
    c.prp1 = (uint64_t)info;
    c.cdw10 = 2;                        /* Liste der Namespaces */
    memset(info, 0, PAGE);
    n->nsid = submit(n, &n->admin, &c, 5000) ? *(uint32_t *)info : 1;
    if (n->nsid == 0)
        n->nsid = 1;

    memset(&c, 0, sizeof(c));
    c.cdw0 = ADMIN_IDENTIFY;
    c.nsid = n->nsid;
    c.prp1 = (uint64_t)info;
    if (!submit(n, &n->admin, &c, 5000))
        return false;
    uint64_t sectors;
    memcpy(&sectors, info, 8);
    uint8_t format = info[26] & 0x0F;
    uint8_t shift = info[128 + 4 * format + 2];
    if (shift != 9) {
        klog("NVMe %d: Sektorgröße %u Bytes wird nicht unterstützt (nur 512)\n", index,
             1u << shift);
        return false;
    }

    /* Eine Warteschlange für Lese- und Schreibaufträge anmelden */
    memset(&c, 0, sizeof(c));
    c.cdw0 = ADMIN_FEATURES;
    c.cdw10 = 0x07;                     /* Anzahl der Warteschlangen: je eine */
    submit(n, &n->admin, &c, 5000);
    memset(&c, 0, sizeof(c));
    c.cdw0 = ADMIN_CREATE_CQ;
    c.prp1 = (uint64_t)n->io.cq;
    c.cdw10 = ((QUEUE_ENTRIES - 1) << 16) | 1;
    c.cdw11 = 1;                        /* zusammenhängender Speicher, ohne Interrupt */
    if (!submit(n, &n->admin, &c, 5000))
        return false;
    memset(&c, 0, sizeof(c));
    c.cdw0 = ADMIN_CREATE_SQ;
    c.prp1 = (uint64_t)n->io.sq;
    c.cdw10 = ((QUEUE_ENTRIES - 1) << 16) | 1;
    c.cdw11 = (1u << 16) | 1;           /* gehört zur Ergebnis-Warteschlange 1 */
    if (!submit(n, &n->admin, &c, 5000))
        return false;

    char name[24];
    ksnprintf(name, sizeof(name), "nvme%d", index);
    n->block = block_register_internal(name, sectors);
    if (!n->block)
        return false;
    n->block->driver_data = n;
    n->block->read = nvme_read;
    n->block->write = nvme_write;
    n->block->flush = nvme_flush;
    klog("NVMe %d: %s, %llu MiB (schreibgeschützt bis auf eine HelloOS-Partition)\n",
         index, model, sectors / 2048);
    return true;
}

void nvme_init(void)
{
    pci_device_t found[CONTROLLERS_MAX];
    int count = pci_find(0x01, 0x08, 0x02, found, CONTROLLERS_MAX);
    for (int i = 0; i < count; i++) {
        nvme_t *n = &controllers[controller_count];
        memset(n, 0, sizeof(*n));
        if (start(n, &found[i], controller_count))
            controller_count++;
        else
            n->dead = true;
    }
}

void nvme_shutdown(void)
{
    for (int i = 0; i < controller_count; i++) {
        nvme_t *n = &controllers[i];
        if (n->dead || n->busy)
            continue;
        /* Dem Laufwerk ankündigen, dass gleich der Strom weg ist */
        write32(n, REG_CC, (read32(n, REG_CC) & ~0xC000u) | 0x4000u);
        uint64_t deadline = time_ms() + 3000;
        while (time_ms() < deadline && ((read32(n, REG_CSTS) >> 2) & 3) != 2)
            cpu_pause();
    }
}
