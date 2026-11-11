/*
 * Ton: Treiber für "Intel HD Audio" (die Soundkarte fast aller PCs), ein kleiner
 * Mischer und die Systemklänge von HelloOS.
 *
 * HD Audio besteht aus zwei Teilen: dem Controller am PCI-Bus, der Daten aus
 * dem Speicher holt, und einem oder mehreren "Codecs" dahinter - den Chips,
 * die daraus das Signal für Lautsprecher und Kopfhörer machen. Ein Codec
 * beschreibt sich selbst als Geflecht aus Bausteinen (Wandler, Mischer,
 * Buchsen); der Treiber fragt dieses Geflecht ab und sucht für jede Buchse
 * einen Weg zu einem Wandler. Gearbeitet wird ohne Interrupts.
 *
 * Die Klänge sind nicht aufgenommen, sondern beim Start aus Sinustönen errechnet.
 */
#include "sound.h"
#include "console.h"
#include "cpu.h"
#include "memory.h"
#include "pci.h"
#include "storage.h"
#include "string.h"
#include "task.h"
#include "time.h"

#define RATE          48000             /* Abtastwerte pro Sekunde, zwei Kanäle, 16 Bit */
#define PAGE          4096
#define RING_BYTES    65536             /* Puffer, den der Controller im Kreis abspielt */
#define RING_FRAMES   (RING_BYTES / 4)
#define AHEAD_FRAMES  (RING_FRAMES / 2) /* so weit wird vorausgefüllt (etwa 170 ms) */
#define VOICES        4
#define NODES_MAX     96
#define CONNS_MAX     16
#define OUTPUTS_MAX   6
#define SETTINGS      "ton.txt"

/* Register des Controllers */
#define REG_GCAP      0x00
#define REG_GCTL      0x08
#define REG_WAKEEN    0x0C
#define REG_STATESTS  0x0E
#define REG_LLCH      0x14
#define REG_INTCTL    0x20
#define REG_CORBLBASE 0x40
#define REG_CORBWP    0x48
#define REG_CORBRP    0x4A
#define REG_CORBCTL   0x4C
#define REG_CORBSIZE  0x4E
#define REG_RIRBLBASE 0x50
#define REG_RIRBWP    0x58
#define REG_RINTCNT   0x5A
#define REG_RIRBCTL   0x5C
#define REG_RIRBSTS   0x5D
#define REG_RIRBSIZE  0x5E
#define REG_ICOI      0x60
#define REG_ICII      0x64
#define REG_ICIS      0x68
/* Register eines Datenstroms (ab 0x80, je 0x20) */
#define SD_CTL        0x00
#define SD_LPIB       0x04
#define SD_CBL        0x08
#define SD_LVI        0x0C
#define SD_FMT        0x12
#define SD_BDPL       0x18
#define SD_BDPU       0x1C

/* Befehle an den Codec */
#define VERB_GET_PARAM      0xF00
#define VERB_GET_CONN_LIST  0xF02
#define VERB_SET_CONN       0x701
#define VERB_SET_POWER      0x705
#define VERB_SET_STREAM     0x706
#define VERB_SET_PIN        0x707
#define VERB_SET_EAPD       0x70C
#define VERB_GET_CONFIG     0xF1C
#define VERB4_SET_FORMAT    0x2
#define VERB4_SET_AMP       0x3
#define PARAM_VENDOR        0x00
#define PARAM_NODE_COUNT    0x04
#define PARAM_GROUP_TYPE    0x05
#define PARAM_WIDGET_CAPS   0x09
#define PARAM_PIN_CAPS      0x0C
#define PARAM_IN_AMP_CAPS   0x0D
#define PARAM_CONN_LENGTH   0x0E
#define PARAM_OUT_AMP_CAPS  0x12

enum { WIDGET_OUTPUT = 0, WIDGET_MIXER = 2, WIDGET_SELECTOR = 3, WIDGET_PIN = 4 };
#define FORMAT_48K_16BIT_STEREO 0x0011
#define STREAM_TAG 1

typedef struct {
    uint32_t caps;
    uint8_t type;
    uint8_t conn_count;
    uint8_t conns[CONNS_MAX];
} node_t;

static struct {
    volatile uint8_t *regs;
    volatile uint8_t *stream;           /* Register des Ausgabestroms */
    uint32_t *corb;
    uint64_t *rirb;
    uint32_t corb_entries, rirb_entries, rirb_read;
    bool immediate;                     /* Befehle einzeln statt über die Warteschlangen */
    bool dead;

    int codec;                          /* Adresse des Codecs, der benutzt wird */
    uint32_t vendor;
    int group, first_node, node_count;
    node_t nodes[NODES_MAX];
    int outputs;                        /* eingerichtete Buchsen */
    char description[96];

    int16_t *ring;
    uint32_t write_frame;               /* nächste Stelle im Ring, die gefüllt wird */
    bool running;
} hda;

static bool available;

/* --- Controller ------------------------------------------------------------------------- */

static uint32_t read32(uint32_t reg)
{
    return *(volatile uint32_t *)(hda.regs + reg);
}

static uint16_t read16(uint32_t reg)
{
    return *(volatile uint16_t *)(hda.regs + reg);
}

static uint8_t read8(uint32_t reg)
{
    return *(volatile uint8_t *)(hda.regs + reg);
}

static void write32(uint32_t reg, uint32_t value)
{
    *(volatile uint32_t *)(hda.regs + reg) = value;
}

static void write16(uint32_t reg, uint16_t value)
{
    *(volatile uint16_t *)(hda.regs + reg) = value;
}

static void write8(uint32_t reg, uint8_t value)
{
    *(volatile uint8_t *)(hda.regs + reg) = value;
}

/* Wartet, bis die Bits in einem Register den gewünschten Wert haben. */
static bool wait32(uint32_t reg, uint32_t mask, uint32_t wanted, uint32_t timeout_ms)
{
    uint64_t deadline = time_ms() + timeout_ms;
    while ((read32(reg) & mask) != wanted) {
        if (time_ms() >= deadline)
            return false;
        cpu_pause();
    }
    return true;
}

/* Schickt einen Befehl an den Codec und liefert dessen Antwort. */
static uint32_t command_raw(uint32_t verb)
{
    if (hda.dead)
        return 0;

    if (hda.immediate) {
        uint64_t deadline = time_ms() + 50;
        while ((read16(REG_ICIS) & 1) && time_ms() < deadline)
            cpu_pause();
        write16(REG_ICIS, 2);           /* alte Antwort verwerfen */
        write32(REG_ICOI, verb);
        write16(REG_ICIS, 1);
        deadline = time_ms() + 50;
        while (!(read16(REG_ICIS) & 2)) {
            if (time_ms() >= deadline) {
                hda.dead = true;
                return 0;
            }
            cpu_pause();
        }
        return read32(REG_ICII);
    }

    uint32_t write = ((read16(REG_CORBWP) & 0xFF) + 1) % hda.corb_entries;
    hda.corb[write] = verb;
    write16(REG_CORBWP, (uint16_t)write);
    uint64_t deadline = time_ms() + 100;
    for (;;) {
        uint32_t position = read16(REG_RIRBWP) & 0xFF;
        if (position != hda.rirb_read) {
            hda.rirb_read = (hda.rirb_read + 1) % hda.rirb_entries;
            uint64_t entry = hda.rirb[hda.rirb_read];
            write8(REG_RIRBSTS, 0x05);  /* Antwort quittieren - sonst stockt die Warteschlange */
            if (entry & (1ull << 36))
                continue;               /* eine Meldung, die niemand angefordert hat */
            return (uint32_t)entry;
        }
        if (time_ms() >= deadline) {
            hda.dead = true;
            return 0;
        }
        cpu_pause();
    }
}

static uint32_t command(int nid, uint32_t verb, uint32_t payload)
{
    return command_raw(((uint32_t)hda.codec << 28) | ((uint32_t)nid << 20) | (verb << 8) |
                       (payload & 0xFF));
}

static uint32_t command4(int nid, uint32_t verb, uint32_t payload)
{
    return command_raw(((uint32_t)hda.codec << 28) | ((uint32_t)nid << 20) | (verb << 16) |
                       (payload & 0xFFFF));
}

static uint32_t parameter(int nid, uint32_t id)
{
    return command(nid, VERB_GET_PARAM, id);
}

/* Größe einer Befehls-Warteschlange einstellen: die größte, die der Controller kann. */
static uint32_t queue_size(uint32_t reg)
{
    uint8_t caps = read8(reg) >> 4;
    uint8_t choice = caps & 4 ? 2 : caps & 2 ? 1 : 0;
    write8(reg, (uint8_t)((read8(reg) & ~3u) | choice));
    return choice == 2 ? 256 : choice == 1 ? 16 : 2;
}

/* Controller zurücksetzen; liefert die Menge der Codecs, die sich melden. */
static uint32_t reset_controller(void)
{
    write32(REG_GCTL, read32(REG_GCTL) & ~1u);
    if (!wait32(REG_GCTL, 1, 0, 200))
        return 0;
    time_delay_ms(1);
    write32(REG_GCTL, read32(REG_GCTL) | 1);
    if (!wait32(REG_GCTL, 1, 1, 200))
        return 0;
    time_delay_ms(5);                   /* die Codecs brauchen einen Moment, um sich zu melden */
    uint64_t deadline = time_ms() + 200;
    while ((read16(REG_STATESTS) & 0x7FFF) == 0 && time_ms() < deadline)
        cpu_pause();
    return read16(REG_STATESTS) & 0x7FFF;
}

/*
 * Auf neueren Intel-Plattformen hängt der Codec an einer eigens schaltbaren
 * Leitung. Meldet sich nach dem Zurücksetzen niemand, wird sie eingeschaltet.
 */
static void power_up_link(void)
{
    uint32_t offset = read16(REG_LLCH);
    for (int guard = 0; guard < 8 && offset >= 0x100 && offset < 0x3000; guard++) {
        uint32_t header = read32(offset);
        if (((header >> 16) & 0xFFF) == 2) {            /* Verzeichnis der Leitungen */
            uint32_t control = offset + 0x40 + 0x04;    /* Leitung 0: die für HD-Audio-Codecs */
            write32(control, read32(control) | (1u << 16));
            wait32(control, 1u << 23, 1u << 23, 100);
            return;
        }
        offset = header & 0xFFFF;
    }
}

static bool start_controller(const pci_device_t *pci)
{
    uint64_t bar = (pci_read32(pci, 0x10) & ~0xFull);
    if ((pci_read32(pci, 0x10) & 0x6) == 0x4)
        bar |= (uint64_t)pci_read32(pci, 0x14) << 32;
    if (bar == 0) {
        klog("Ton: Soundkarte ohne nutzbare Register - bleibt stumm\n");
        return false;
    }
    pci_write16(pci, 0x04, (uint16_t)(pci_read16(pci, 0x04) | 0x0006 | 0x0400));
    if (pci->vendor_id == 0x8086)       /* Intel: Vorrangklasse der Daten auf den Normalwert */
        pci_write32(pci, 0x44, pci_read32(pci, 0x44) & ~0x07u);
    vmm_map(bar, 0x4000);
    hda.regs = (volatile uint8_t *)bar;

    uint32_t codecs = reset_controller();
    if (!codecs && pci->vendor_id == 0x8086) {
        /* Zweiter Versuch: Taktabschaltung aussetzen und die Codec-Leitung einschalten */
        uint32_t gating = pci_read32(pci, 0x48);
        pci_write32(pci, 0x48, gating & ~(1u << 6));
        power_up_link();
        codecs = reset_controller();
        if (!codecs) {
            power_up_link();
            time_delay_ms(20);
            codecs = read16(REG_STATESTS) & 0x7FFF;
        }
        pci_write32(pci, 0x48, gating);
    }
    if (!codecs) {
        klog("Ton: Soundkarte gefunden, aber kein Codec meldet sich - bleibt stumm\n");
        return false;
    }
    write16(REG_WAKEEN, 0);
    write32(REG_INTCTL, 0);             /* keine Interrupts */
    write16(REG_STATESTS, 0x7FFF);

    /* Warteschlangen für Befehle und Antworten */
    hda.corb = alloc_pages(1);
    hda.rirb = alloc_pages(1);
    if (!hda.corb || !hda.rirb)
        return false;
    write8(REG_CORBCTL, 0);
    write8(REG_RIRBCTL, 0);
    hda.corb_entries = queue_size(REG_CORBSIZE);
    hda.rirb_entries = queue_size(REG_RIRBSIZE);
    write32(REG_CORBLBASE, (uint32_t)(uint64_t)hda.corb);
    write32(REG_CORBLBASE + 4, (uint32_t)((uint64_t)hda.corb >> 32));
    write16(REG_CORBRP, 0x8000);        /* Lesezeiger zurücksetzen */
    time_delay_ms(1);
    write16(REG_CORBRP, 0);
    time_delay_ms(1);
    write16(REG_CORBWP, 0);
    write32(REG_RIRBLBASE, (uint32_t)(uint64_t)hda.rirb);
    write32(REG_RIRBLBASE + 4, (uint32_t)((uint64_t)hda.rirb >> 32));
    write16(REG_RIRBWP, 0x8000);
    write16(REG_RINTCNT, 1);
    hda.rirb_read = 0;
    write8(REG_CORBCTL, 2);
    /*
     * Antworten empfangen und ihren Eingang im Statusregister vermerken lassen.
     * Ein Interrupt entsteht daraus nicht - die sind oben insgesamt abgeschaltet.
     */
    write8(REG_RIRBCTL, 3);

    /* Den ersten Codec nehmen, der antwortet und einen Tonteil hat */
    for (int address = 0; address < 15; address++) {
        if (!(codecs & (1u << address)))
            continue;
        hda.codec = address;
        hda.dead = false;
        hda.immediate = false;
        uint32_t vendor = parameter(0, PARAM_VENDOR);
        if (hda.dead) {                 /* Warteschlangen streiken: Befehle einzeln schicken */
            hda.dead = false;
            hda.immediate = true;
            write8(REG_CORBCTL, 0);
            write8(REG_RIRBCTL, 0);
            vendor = parameter(0, PARAM_VENDOR);
        }
        if (hda.dead || vendor == 0 || vendor == 0xFFFFFFFF)
            continue;
        uint32_t groups = parameter(0, PARAM_NODE_COUNT);
        int first = (groups >> 16) & 0xFF, count = groups & 0xFF;
        for (int nid = first; nid < first + count; nid++) {
            if ((parameter(nid, PARAM_GROUP_TYPE) & 0xFF) == 1) {       /* Tonteil */
                hda.vendor = vendor;
                hda.group = nid;
                return true;
            }
        }
    }
    klog("Ton: Der Codec der Soundkarte antwortet nicht wie erwartet - bleibt stumm\n");
    return false;
}

/* --- Codec: Bausteine abfragen und Wege einrichten ----------------------------------------- */

static node_t *node(int nid)
{
    int index = nid - hda.first_node;
    return index >= 0 && index < hda.node_count ? &hda.nodes[index] : NULL;
}

static void read_nodes(void)
{
    uint32_t count = parameter(hda.group, PARAM_NODE_COUNT);
    hda.first_node = (count >> 16) & 0xFF;
    hda.node_count = count & 0xFF;
    if (hda.node_count > NODES_MAX)
        hda.node_count = NODES_MAX;

    for (int i = 0; i < hda.node_count; i++) {
        int nid = hda.first_node + i;
        node_t *n = &hda.nodes[i];
        n->caps = parameter(nid, PARAM_WIDGET_CAPS);
        n->type = (n->caps >> 20) & 0x0F;
        n->conn_count = 0;
        if (!(n->caps & (1u << 8)))
            continue;                   /* hat keine Eingänge */

        uint32_t length = parameter(nid, PARAM_CONN_LENGTH);
        if (length & 0x80)
            continue;                   /* lange Form der Liste: kommt praktisch nicht vor */
        int total = length & 0x7F;
        int previous = 0;
        for (int at = 0; at < total && n->conn_count < CONNS_MAX; at += 4) {
            uint32_t entries = command(nid, VERB_GET_CONN_LIST, (uint32_t)at);
            for (int k = 0; k < 4 && at + k < total; k++) {
                int value = (entries >> (8 * k)) & 0xFF;
                int target = value & 0x7F;
                /* Das oberste Bit bedeutet "alle von der vorigen Nummer bis hierher" */
                int from = (value & 0x80) && previous ? previous + 1 : target;
                for (int t = from; t <= target && n->conn_count < CONNS_MAX; t++)
                    n->conns[n->conn_count++] = (uint8_t)t;
                previous = target;
            }
        }
    }
}

/* Lautstärkesteller eines Bausteins auf "unverändert durchlassen" (0 dB), nicht stumm. */
static void set_amp(int nid, bool output, int index)
{
    const node_t *n = node(nid);
    uint32_t caps = parameter((n->caps & (1u << 3)) ? nid : hda.group,
                              output ? PARAM_OUT_AMP_CAPS : PARAM_IN_AMP_CAPS);
    uint32_t gain = caps & 0x7F;        /* Stufe, die 0 dB entspricht */
    command4(nid, VERB4_SET_AMP,
             (output ? 0x8000u : 0x4000u) | 0x3000u | ((uint32_t)index << 8) | gain);
}

/*
 * Sucht von einem Baustein aus einen Weg zu einem Wandler und richtet ihn
 * dabei ein. Liefert die Nummer des Wandlers oder 0.
 */
static int build_path(int nid, int depth)
{
    node_t *n = node(nid);
    if (!n || depth > 6 || (n->caps & (1u << 9)))       /* digitale Bausteine auslassen */
        return 0;

    if (n->caps & (1u << 10))
        command(nid, VERB_SET_POWER, 0);                /* einschalten */
    if (n->type == WIDGET_OUTPUT) {
        command(nid, VERB_SET_STREAM, STREAM_TAG << 4);
        command4(nid, VERB4_SET_FORMAT, FORMAT_48K_16BIT_STEREO);
        if (n->caps & (1u << 2))
            set_amp(nid, true, 0);
        return nid;
    }
    if (n->type != WIDGET_MIXER && n->type != WIDGET_SELECTOR && n->type != WIDGET_PIN)
        return 0;

    for (int i = 0; i < n->conn_count; i++) {
        int dac = build_path(n->conns[i], depth + 1);
        if (!dac)
            continue;
        if (n->type != WIDGET_MIXER && n->conn_count > 1)
            command(nid, VERB_SET_CONN, (uint32_t)i);   /* diesen Eingang wählen */
        if (n->type != WIDGET_PIN && (n->caps & (1u << 1)))
            set_amp(nid, false, i);
        if (n->caps & (1u << 2))
            set_amp(nid, true, 0);
        return dac;
    }
    return 0;
}

static void setup_outputs(void)
{
    static const char *const kinds[] = { "Line-Ausgang", "Lautsprecher", "Kopfhörer" };
    char list[64] = "";

    command(hda.group, VERB_SET_POWER, 0);
    time_delay_ms(10);
    read_nodes();

    /* Zwei Durchgänge: erst Buchsen, die als Ausgang ausgewiesen sind, sonst jede geeignete */
    for (int pass = 0; pass < 2 && hda.outputs == 0; pass++) {
        for (int i = 0; i < hda.node_count && hda.outputs < OUTPUTS_MAX; i++) {
            int nid = hda.first_node + i;
            const node_t *n = &hda.nodes[i];
            if (n->type != WIDGET_PIN || (n->caps & (1u << 9)))
                continue;
            uint32_t pin_caps = parameter(nid, PARAM_PIN_CAPS);
            uint32_t config = command(nid, VERB_GET_CONFIG, 0);
            uint32_t device = (config >> 20) & 0x0F;
            bool connected = (config >> 30) != 1;       /* 1: "nichts angeschlossen" */
            if (!(pin_caps & (1u << 4)) || (pass == 0 && (!connected || device > 2)))
                continue;
            if (!build_path(nid, 0))
                continue;
            /* Ausgang einschalten; Kopfhörer bekommen den kräftigeren Verstärker */
            command(nid, VERB_SET_PIN, 0x40 | (device == 2 && (pin_caps & (1u << 3)) ? 0x80 : 0));
            if (pin_caps & (1u << 16))
                command(nid, VERB_SET_EAPD, 0x02);      /* externen Verstärker einschalten */
            hda.outputs++;
            const char *kind = device <= 2 ? kinds[device] : "Ausgang";
            size_t used = strlen(list);
            if (used + strlen(kind) + 3 < sizeof(list))
                ksnprintf(list + used, sizeof(list) - used, "%s%s", used ? ", " : "", kind);
        }
    }
    ksnprintf(hda.description, sizeof(hda.description), "HD Audio, Codec %04x:%04x, %s",
              hda.vendor >> 16, hda.vendor & 0xFFFF, hda.outputs ? list : "kein Ausgang gefunden");
}

/* --- Datenstrom -------------------------------------------------------------------------- */

static bool setup_stream(void)
{
    uint32_t gcap = read16(REG_GCAP);
    uint32_t inputs = (gcap >> 8) & 0x0F, outputs = (gcap >> 12) & 0x0F;
    if (outputs == 0)
        return false;
    hda.stream = hda.regs + 0x80 + inputs * 0x20;      /* erster Ausgabestrom */

    hda.ring = alloc_pages(RING_BYTES / PAGE);
    uint32_t *list = alloc_pages(1);
    if (!hda.ring || !list)
        return false;
    /* Die Liste der Pufferstücke: zwei Hälften des Rings, endlos wiederholt */
    for (int i = 0; i < 2; i++) {
        uint64_t address = (uint64_t)hda.ring + (uint64_t)i * (RING_BYTES / 2);
        list[4 * i] = (uint32_t)address;
        list[4 * i + 1] = (uint32_t)(address >> 32);
        list[4 * i + 2] = RING_BYTES / 2;
        list[4 * i + 3] = 0;
    }

    volatile uint32_t *control = (volatile uint32_t *)(hda.stream + SD_CTL);
    *control &= ~2u;                    /* anhalten */
    *control |= 1;                      /* zurücksetzen ... */
    uint64_t deadline = time_ms() + 100;
    while (!(*control & 1) && time_ms() < deadline)
        cpu_pause();
    *control &= ~1u;
    deadline = time_ms() + 100;
    while ((*control & 1) && time_ms() < deadline)
        cpu_pause();

    *control = (uint32_t)STREAM_TAG << 20;
    *(volatile uint32_t *)(hda.stream + SD_CBL) = RING_BYTES;
    *(volatile uint16_t *)(hda.stream + SD_LVI) = 1;
    *(volatile uint16_t *)(hda.stream + SD_FMT) = FORMAT_48K_16BIT_STEREO;
    *(volatile uint32_t *)(hda.stream + SD_BDPL) = (uint32_t)(uint64_t)list;
    *(volatile uint32_t *)(hda.stream + SD_BDPU) = (uint32_t)((uint64_t)list >> 32);
    return true;
}

static void stream_run(bool run)
{
    volatile uint32_t *control = (volatile uint32_t *)(hda.stream + SD_CTL);
    if (run)
        *control |= 2;
    else
        *control &= ~2u;
    hda.running = run;
}

/* Wo der Controller gerade abspielt (in Abtastwert-Paaren ab Ringanfang). */
static uint32_t play_frame(void)
{
    return (*(volatile uint32_t *)(hda.stream + SD_LPIB) % RING_BYTES) / 4;
}

/* --- Klänge errechnen --------------------------------------------------------------------- */

/* Ein Viertel einer Sinusschwingung; der Rest ergibt sich durch Spiegeln. */
static const int16_t quarter_sine[257] = {
        0,   201,   402,   603,   804,  1005,  1206,  1407,  1608,  1809,  2009,  2210,
     2410,  2611,  2811,  3012,  3212,  3412,  3612,  3811,  4011,  4210,  4410,  4609,
     4808,  5007,  5205,  5404,  5602,  5800,  5998,  6195,  6393,  6590,  6786,  6983,
     7179,  7375,  7571,  7767,  7962,  8157,  8351,  8545,  8739,  8933,  9126,  9319,
     9512,  9704,  9896, 10087, 10278, 10469, 10659, 10849, 11039, 11228, 11417, 11605,
    11793, 11980, 12167, 12353, 12539, 12725, 12910, 13094, 13279, 13462, 13645, 13828,
    14010, 14191, 14372, 14553, 14732, 14912, 15090, 15269, 15446, 15623, 15800, 15976,
    16151, 16325, 16499, 16673, 16846, 17018, 17189, 17360, 17530, 17700, 17869, 18037,
    18204, 18371, 18537, 18703, 18868, 19032, 19195, 19357, 19519, 19680, 19841, 20000,
    20159, 20317, 20475, 20631, 20787, 20942, 21096, 21250, 21403, 21554, 21705, 21856,
    22005, 22154, 22301, 22448, 22594, 22739, 22884, 23027, 23170, 23311, 23452, 23592,
    23731, 23870, 24007, 24143, 24279, 24413, 24547, 24680, 24811, 24942, 25072, 25201,
    25329, 25456, 25582, 25708, 25832, 25955, 26077, 26198, 26319, 26438, 26556, 26674,
    26790, 26905, 27019, 27133, 27245, 27356, 27466, 27575, 27683, 27790, 27896, 28001,
    28105, 28208, 28310, 28411, 28510, 28609, 28706, 28803, 28898, 28992, 29085, 29177,
    29268, 29358, 29447, 29534, 29621, 29706, 29791, 29874, 29956, 30037, 30117, 30195,
    30273, 30349, 30424, 30498, 30571, 30643, 30714, 30783, 30852, 30919, 30985, 31050,
    31113, 31176, 31237, 31297, 31356, 31414, 31470, 31526, 31580, 31633, 31685, 31736,
    31785, 31833, 31880, 31926, 31971, 32014, 32057, 32098, 32137, 32176, 32213, 32250,
    32285, 32318, 32351, 32382, 32412, 32441, 32469, 32495, 32521, 32545, 32567, 32589,
    32609, 32628, 32646, 32663, 32678, 32692, 32705, 32717, 32728, 32737, 32745, 32752,
    32757, 32761, 32765, 32766, 32767,
};

/* Sinus für eine Phase von 0 bis 2^32 (eine volle Schwingung), Ergebnis -32767 bis 32767. */
static int32_t sine(uint32_t phase)
{
    uint32_t index = (phase >> 22) & 0x3FF;             /* 1024 Schritte pro Schwingung */
    int32_t value = index < 256 ? quarter_sine[index]
                  : index < 512 ? quarter_sine[512 - index]
                  : index < 768 ? -quarter_sine[index - 512] : -quarter_sine[1024 - index];
    return value;
}

typedef struct {
    int16_t *samples;                   /* ein Kanal */
    uint32_t frames;
} clip_t;

static clip_t clips[SOUND_COUNT];

/*
 * Mischt einen glockenartigen Ton in den Klang: schnell da, langsam
 * verklingend, mit etwas Oberton für einen wärmeren Klang.
 */
static void add_tone(clip_t *clip, uint32_t start_ms, uint32_t length_ms, uint32_t hertz,
                     int32_t loudness)
{
    uint32_t start = start_ms * (RATE / 1000), length = length_ms * (RATE / 1000);
    uint32_t step = (uint32_t)(((uint64_t)hertz << 32) / RATE);
    uint32_t phase = 0;
    uint32_t attack = RATE / 200;       /* 5 ms einblenden - vermeidet Knackser */
    uint32_t release = RATE / 50;       /* am Ende 20 ms ausblenden */
    /* Hüllkurve: fällt über die Tonlänge gleichmäßig auf etwa ein Zwanzigstel */
    uint64_t envelope = 1ull << 32;
    uint64_t factor = (1ull << 32) - ((3ull << 32) / (length ? length : 1));

    for (uint32_t i = 0; i < length && start + i < clip->frames; i++) {
        int32_t wave = sine(phase) + sine(phase * 2) / 4 + sine(phase * 3) / 10;
        int64_t value = (int64_t)wave * loudness / 100;
        value = value * (int64_t)(envelope >> 16) / 65536;
        if (i < attack)
            value = value * i / attack;
        if (length - i < release)
            value = value * (length - i) / release;
        int32_t mixed = clip->samples[start + i] + (int32_t)value;
        clip->samples[start + i] = (int16_t)(mixed > 32767 ? 32767 : mixed < -32768 ? -32768 : mixed);
        phase += step;
        envelope = envelope * factor >> 32;
    }
}

static bool make_clip(clip_t *clip, uint32_t length_ms)
{
    clip->frames = length_ms * (RATE / 1000);
    clip->samples = kzalloc((size_t)clip->frames * sizeof(int16_t));
    return clip->samples != NULL;
}

static bool make_clips(void)
{
    if (!make_clip(&clips[SOUND_STARTUP], 2200) || !make_clip(&clips[SOUND_SHUTDOWN], 1700) ||
        !make_clip(&clips[SOUND_NOTIFY], 600) || !make_clip(&clips[SOUND_ERROR], 650))
        return false;

    /* Start: ein aufsteigender heller Akkord (D-Dur mit Sexte), der ausklingt */
    add_tone(&clips[SOUND_STARTUP], 0, 1500, 294, 22);
    add_tone(&clips[SOUND_STARTUP], 140, 1500, 440, 20);
    add_tone(&clips[SOUND_STARTUP], 280, 1500, 587, 18);
    add_tone(&clips[SOUND_STARTUP], 420, 1700, 740, 16);
    add_tone(&clips[SOUND_STARTUP], 560, 1600, 988, 12);

    /* Ende: dieselben Töne abwärts */
    add_tone(&clips[SOUND_SHUTDOWN], 0, 900, 740, 18);
    add_tone(&clips[SOUND_SHUTDOWN], 150, 900, 587, 18);
    add_tone(&clips[SOUND_SHUTDOWN], 300, 1000, 440, 20);
    add_tone(&clips[SOUND_SHUTDOWN], 450, 1200, 294, 22);

    /* Hinweis: zwei kurze Töne aufwärts */
    add_tone(&clips[SOUND_NOTIFY], 0, 300, 880, 24);
    add_tone(&clips[SOUND_NOTIFY], 130, 450, 1175, 24);

    /* Fehler: zwei tiefere Töne abwärts */
    add_tone(&clips[SOUND_ERROR], 0, 300, 330, 30);
    add_tone(&clips[SOUND_ERROR], 170, 460, 233, 30);
    return true;
}

/* --- Mischer ------------------------------------------------------------------------------- */

static struct {
    const clip_t *clip;
    uint32_t position;
} voices[VOICES];

static int volume = 70;                 /* 0 bis 100 */
static bool settings_dirty;
static uint32_t sound_pid;

/* Füllt count Abtastwert-Paare ab der Schreibstelle; liefert, ob noch etwas klingt. */
static bool mix(uint32_t count)
{
    bool active = false;
    for (uint32_t i = 0; i < count; i++) {
        int32_t sum = 0;
        for (int v = 0; v < VOICES; v++) {
            if (!voices[v].clip)
                continue;
            sum += voices[v].clip->samples[voices[v].position++];
            if (voices[v].position >= voices[v].clip->frames)
                voices[v].clip = NULL;
            active = true;
        }
        sum = sum * volume / 100;
        int16_t sample = (int16_t)(sum > 32767 ? 32767 : sum < -32768 ? -32768 : sum);
        hda.ring[2 * hda.write_frame] = sample;
        hda.ring[2 * hda.write_frame + 1] = sample;
        hda.write_frame = (hda.write_frame + 1) % RING_FRAMES;
    }
    return active;
}

static bool any_voice(void)
{
    for (int v = 0; v < VOICES; v++) {
        if (voices[v].clip)
            return true;
    }
    return false;
}

static void load_settings(void)
{
    char *text;
    size_t size;
    uint32_t value;
    if (!storage_load(STORAGE_SETTINGS, SETTINGS, 16, &text, &size))
        return;
    for (size_t i = 0; i < size; i++) {
        if (text[i] < '0' || text[i] > '9')
            text[i] = '\0';
    }
    if (parse_uint(text, &value) && value <= 100)
        volume = (int)value;
    kfree(text);
}

static void sound_task(void *arg)
{
    (void)arg;
    bool loaded = false;
    uint32_t silence = RING_FRAMES;     /* so viele stille Paare stehen schon im Ring */

    sound_play(SOUND_STARTUP);
    for (;;) {
        /* Einstellungen: laden, sobald der Datenträger da ist; sichern nach Änderungen */
        if (!loaded && storage_available()) {
            load_settings();
            loaded = true;
        }
        if (settings_dirty && storage_available()) {
            char text[8];
            settings_dirty = false;
            int n = ksnprintf(text, sizeof(text), "%d\n", volume);
            storage_save(STORAGE_SETTINGS, SETTINGS, text, (size_t)n);
        }

        if (!hda.running) {
            if (!any_voice()) {
                task_sleep(40);
                continue;
            }
            /* Es geht los: Schreibstelle knapp vor die Abspielstelle setzen */
            hda.write_frame = (play_frame() + 256) % RING_FRAMES;
            mix(AHEAD_FRAMES);
            silence = 0;
            stream_run(true);
        }

        uint32_t ahead = (hda.write_frame + RING_FRAMES - play_frame()) % RING_FRAMES;
        if (ahead < AHEAD_FRAMES) {
            uint32_t count = AHEAD_FRAMES - ahead;
            silence = mix(count) ? 0 : silence + count;
        }
        /* Nach dem letzten Klang noch den Vorrat abspielen lassen, dann anhalten */
        if (silence >= RING_FRAMES) {
            stream_run(false);
            continue;
        }
        task_sleep(10);
    }
}

/* --- Schnittstelle ------------------------------------------------------------------------ */

void sound_init(void)
{
    pci_device_t found[2];
    /* Klasse "Ton": HD Audio; neuere Intel-Chips melden sich auch als "Audiogerät" */
    int count = pci_find(0x04, 0x03, 0x00, found, 2);
    if (count == 0) {
        count = pci_find(0x04, 0x01, 0x00, found, 1);
        if (count && found[0].vendor_id != 0x8086)
            count = 0;
    }

    for (int i = 0; i < count && !available; i++) {
        memset(&hda, 0, sizeof(hda));
        if (!start_controller(&found[i]))
            continue;
        setup_outputs();
        if (hda.dead || hda.outputs == 0 || !setup_stream() || !make_clips()) {
            klog("Ton: %s - bleibt stumm\n", hda.outputs ? "Einrichtung fehlgeschlagen"
                                                         : hda.description);
            continue;
        }
        available = true;
    }
    if (!available) {
        if (count == 0)
            klog("Ton: keine unterstützte Soundkarte gefunden\n");
        return;
    }
    klog("Ton: %s\n", hda.description);
    int pid = task_create("ton", sound_task, NULL, NULL);
    sound_pid = pid > 0 ? (uint32_t)pid : 0;
}

bool sound_available(void)
{
    return available;
}

const char *sound_describe(void)
{
    return available ? hda.description : "keine unterstützte Soundkarte";
}

void sound_play(sound_id_t id)
{
    if (!available || id < 0 || id >= SOUND_COUNT || volume == 0)
        return;
    uint64_t flags = irq_save();
    int slot = 0;
    for (int v = 0; v < VOICES; v++) {
        if (!voices[v].clip) {
            slot = v;
            break;
        }
        /* Alles belegt: die Stimme ersetzen, die am weitesten ist */
        if (voices[v].position > voices[slot].position)
            slot = v;
    }
    voices[slot].position = 0;
    voices[slot].clip = &clips[id];
    irq_restore(flags);
    if (sound_pid)
        task_wake(sound_pid);
}

void sound_wait(uint32_t timeout_ms)
{
    uint64_t deadline = time_ms() + timeout_ms;
    while (available && (any_voice() || hda.running) && time_ms() < deadline)
        task_sleep(20);
}

int sound_volume(void)
{
    return volume;
}

void sound_set_volume(int percent)
{
    volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    settings_dirty = true;
}
