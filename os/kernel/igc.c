/*
 * Treiber für Intels 2,5-Gigabit-Netzwerkchips I225 und I226 (wie sie in
 * vielen aktuellen PCs und Mini-PCs stecken) und ihre nahen Verwandten mit
 * 1 Gigabit (82576, I210, I211).
 *
 * Diese Chips arbeiten wie die e1000-Familie mit Ringen aus Deskriptoren,
 * benutzen aber ein neueres Deskriptorformat und andere Registeradressen.
 * Gearbeitet wird ohne Interrupts: Der Netzwerk-Task fragt regelmäßig nach.
 *
 * Hat der Rechner mehrere Anschlüsse, wird der benutzt, an dem ein Kabel
 * steckt; wandert das Kabel, wandert HelloOS mit.
 *
 * Geprüft ist der Treiber in QEMU mit dem dort nachgebildeten 82576. I225
 * und I226 bildet QEMU nicht nach - für sie richtet er sich nach Intels
 * Unterlagen und ist auf echter Hardware noch nicht gelaufen.
 */
#include "console.h"
#include "cpu.h"
#include "memory.h"
#include "net.h"
#include "pci.h"
#include "string.h"
#include "time.h"

#define REG_CTRL    0x0000
#define REG_STATUS  0x0008
#define REG_EECD    0x0010
#define REG_MDIC    0x0020
#define REG_RCTL    0x0100
#define REG_TCTL    0x0400
#define REG_ICR     0x1500
#define REG_IMC     0x150C
#define REG_EIMC    0x1528
#define REG_CRCERRS 0x4000              /* Zähler: löschen sich beim Lesen */
#define REG_RXERRC  0x400C
#define REG_MPC     0x4010
#define REG_GPRC    0x4074
#define REG_GPTC    0x4080
#define REG_RNBC    0x40A0
#define REG_MTA     0x5200
#define REG_RAL     0x5400
#define REG_RAH     0x5404
/* Empfangs- und Sendering Nummer 0 */
#define REG_RDBAL   0xC000
#define REG_RDBAH   0xC004
#define REG_RDLEN   0xC008
#define REG_SRRCTL  0xC00C
#define REG_RDH     0xC010
#define REG_RDT     0xC018
#define REG_RXDCTL  0xC028
#define REG_TDBAL   0xE000
#define REG_TDBAH   0xE004
#define REG_TDLEN   0xE008
#define REG_TDH     0xE010
#define REG_TDT     0xE018
#define REG_TXDCTL  0xE028

#define CTRL_MASTER_OFF  (1u << 2)      /* keine eigenen Buszugriffe mehr beginnen */
#define CTRL_SLU         (1u << 6)      /* Verbindung aufbauen */
#define CTRL_FORCE_SPEED (1u << 11)
#define CTRL_FORCE_DUPLEX (1u << 12)
#define CTRL_RESET       (1u << 26)
#define STATUS_LINK      (1u << 1)
#define STATUS_MASTER_ON (1u << 19)
#define STATUS_SPEED_100  (1u << 6)
#define STATUS_SPEED_1000 (1u << 7)
#define STATUS_SPEED_2500 (1u << 22)
#define EECD_AUTO_READ   (1u << 9)      /* Einstellungen aus dem Festspeicher geladen */

#define RCTL_ENABLE    (1u << 1)
#define RCTL_BROADCAST (1u << 15)
#define RCTL_STRIP_CRC (1u << 26)
#define TCTL_ENABLE    (1u << 1)
#define TCTL_PAD       (1u << 3)
#define TCTL_RETRY_LATE (1u << 24)
#define QUEUE_ENABLE   (1u << 25)
#define SRRCTL_ONE_BUFFER (1u << 25)    /* neues Format, ein Puffer je Paket */

#define MDIC_WRITE (1u << 26)
#define MDIC_READ  (1u << 27)
#define MDIC_READY (1u << 28)
#define MDIC_ERROR (1u << 30)
#define PHY_POWER_DOWN (1u << 11)
#define PHY_AUTONEG    (1u << 12)
#define PHY_RESTART    (1u << 9)

/* Sendedeskriptor: Länge und Kennzeichen */
#define TX_DATA        0x00300000u      /* Datendeskriptor im neuen Format */
#define TX_END         0x01000000u      /* letztes Stück des Pakets */
#define TX_ADD_CRC     0x02000000u
#define TX_REPORT      0x08000000u
#define TX_EXTENDED    0x20000000u
/* Empfangsdeskriptor nach dem Eintreffen eines Pakets */
#define RX_DONE        0x01u
#define RX_END         0x02u

#define PORTS_MAX   4
#define RX_COUNT    128
#define TX_COUNT    64
#define BUFFER_SIZE 2048

typedef struct {
    uint64_t address;
    uint32_t command;                   /* Länge und Kennzeichen */
    uint32_t status;                    /* beim Senden: Gesamtlänge ab Bit 14 */
} tx_desc_t;

typedef struct {
    uint64_t address;                   /* nach dem Empfang: Auskünfte der Karte */
    uint32_t status;                    /* vor dem Empfang: Adresse eines Kopfpuffers (0) */
    uint16_t length;
    uint16_t vlan;
} rx_desc_t;

typedef struct {
    volatile uint8_t *mmio;
    uint8_t mac[6];
    rx_desc_t *rx;
    tx_desc_t *tx;
    uint8_t *rx_buffers, *tx_buffers;
    pci_device_t pci;
    uint32_t rx_next, tx_next;
    uint32_t sent, received, missed, bad;   /* Zähler der Karte, aufsummiert */
    bool modern;                        /* I225/I226 (sonst: 82576, I210, I211) */
    bool ok;
} port_t;

static port_t ports[PORTS_MAX];
static int port_count;
static int active;                      /* Anschluss, über den gerade alles läuft */
static nic_t nic;

static const struct {
    uint16_t device;
    bool modern;
} device_ids[] = {
    /* I225 und I226 (2,5 Gbit) */
    { 0x15F2, true }, { 0x15F3, true }, { 0x15F8, true }, { 0x15F7, true },
    { 0x3100, true }, { 0x3101, true }, { 0x3102, true }, { 0x5502, true },
    { 0x5503, true }, { 0x0D9F, true }, { 0x125B, true }, { 0x125C, true },
    { 0x125D, true }, { 0x125E, true },
    /* 82576, I210, I211 (1 Gbit) */
    { 0x10C9, false }, { 0x1533, false }, { 0x1539, false }, { 0x157B, false },
};

static uint32_t reg_read(port_t *p, uint32_t reg)
{
    return *(volatile uint32_t *)(p->mmio + reg);
}

static void reg_write(port_t *p, uint32_t reg, uint32_t value)
{
    *(volatile uint32_t *)(p->mmio + reg) = value;
}

/* Wartet, bis die Bits im Register den gewünschten Wert haben. */
static bool reg_wait(port_t *p, uint32_t reg, uint32_t mask, uint32_t wanted,
                     uint32_t timeout_ms)
{
    uint64_t deadline = time_ms() + timeout_ms;
    while ((reg_read(p, reg) & mask) != wanted) {
        if (time_ms() >= deadline)
            return false;
        cpu_pause();
    }
    return true;
}

/* Register des Leitungsbausteins (PHY) lesen und schreiben */
static bool phy_access(port_t *p, uint32_t reg, bool write, uint16_t *value)
{
    /* Der eingebaute Baustein hat bei I225/I226 die Adresse 0, bei den älteren die 1. */
    uint32_t address = p->modern ? 0 : 1;
    reg_write(p, REG_MDIC, (write ? MDIC_WRITE | *value : MDIC_READ) | (reg << 16) |
                           (address << 21));
    if (!reg_wait(p, REG_MDIC, MDIC_READY, MDIC_READY, 20))
        return false;
    uint32_t result = reg_read(p, REG_MDIC);
    if (result & MDIC_ERROR)
        return false;
    if (!write)
        *value = (uint16_t)result;
    return true;
}

static bool start_port(port_t *p, const pci_device_t *pci)
{
    uint64_t bar = pci_read32(pci, 0x10);
    if ((bar & 0x6) == 0x4)
        bar |= (uint64_t)pci_read32(pci, 0x14) << 32;
    bar &= ~0xFull;
    if (!bar)
        return false;
    pci_enable(pci);
    p->pci = *pci;
    vmm_map(bar, 0x20000);
    p->mmio = (volatile uint8_t *)bar;

    /* Ruhe herstellen, dann zurücksetzen - in der Reihenfolge, die Intel vorgibt */
    reg_write(p, REG_CTRL, reg_read(p, REG_CTRL) | CTRL_MASTER_OFF);
    reg_wait(p, REG_STATUS, STATUS_MASTER_ON, 0, 100);
    reg_write(p, REG_IMC, 0xFFFFFFFF);
    reg_write(p, REG_EIMC, 0xFFFFFFFF);
    reg_write(p, REG_RCTL, 0);
    reg_write(p, REG_TCTL, TCTL_PAD);
    time_delay_ms(10);
    reg_write(p, REG_CTRL, reg_read(p, REG_CTRL) | CTRL_RESET);
    time_delay_ms(2);
    reg_wait(p, REG_CTRL, CTRL_RESET, 0, 100);
    reg_wait(p, REG_EECD, EECD_AUTO_READ, EECD_AUTO_READ, 500);
    reg_write(p, REG_IMC, 0xFFFFFFFF);
    reg_write(p, REG_EIMC, 0xFFFFFFFF);
    reg_read(p, REG_ICR);

    /* Die eigene Adresse hat die Karte aus ihrem Festspeicher geladen */
    uint32_t low = reg_read(p, REG_RAL), high = reg_read(p, REG_RAH);
    memcpy(p->mac, &low, 4);
    memcpy(p->mac + 4, &high, 2);
    bool empty = !(p->mac[0] | p->mac[1] | p->mac[2] | p->mac[3] | p->mac[4] | p->mac[5]);
    bool ones = (p->mac[0] & p->mac[1] & p->mac[2] & p->mac[3] & p->mac[4] & p->mac[5]) == 0xFF;
    if (empty || ones || (p->mac[0] & 1))
        return false;                   /* keine brauchbare Adresse */
    reg_write(p, REG_RAL, low);
    reg_write(p, REG_RAH, (high & 0xFFFF) | (1u << 31));
    for (int i = 0; i < 128; i++)
        reg_write(p, REG_MTA + 4 * i, 0);

    /* Verbindung: aushandeln lassen, nichts erzwingen */
    uint32_t ctrl = reg_read(p, REG_CTRL);
    ctrl &= ~(CTRL_FORCE_SPEED | CTRL_FORCE_DUPLEX | CTRL_MASTER_OFF);
    reg_write(p, REG_CTRL, ctrl | CTRL_SLU);
    /*
     * Den Leitungsbaustein wecken (das vorige System kann ihn schlafen gelegt
     * haben) und die Geschwindigkeit neu aushandeln lassen.
     */
    uint16_t control;
    if (phy_access(p, 0, false, &control)) {
        control = (uint16_t)((control & ~PHY_POWER_DOWN) | PHY_AUTONEG | PHY_RESTART);
        phy_access(p, 0, true, &control);
    }

    /* Ringe und Puffer */
    p->rx = alloc_pages(1);
    p->tx = alloc_pages(1);
    p->rx_buffers = alloc_pages(RX_COUNT * BUFFER_SIZE / 4096);
    p->tx_buffers = alloc_pages(TX_COUNT * BUFFER_SIZE / 4096);
    if (!p->rx || !p->tx || !p->rx_buffers || !p->tx_buffers)
        return false;
    for (int i = 0; i < RX_COUNT; i++) {
        p->rx[i].address = (uint64_t)(p->rx_buffers + (size_t)i * BUFFER_SIZE);
        p->rx[i].status = 0;
        p->rx[i].length = 0;
        p->rx[i].vlan = 0;
    }

    /* Empfang: Ring anmelden, einschalten, dann der Karte alle Puffer überlassen */
    reg_write(p, REG_RXDCTL, 0);
    reg_write(p, REG_RDBAL, (uint32_t)(uint64_t)p->rx);
    reg_write(p, REG_RDBAH, (uint32_t)((uint64_t)p->rx >> 32));
    reg_write(p, REG_RDLEN, RX_COUNT * sizeof(rx_desc_t));
    reg_write(p, REG_RDH, 0);
    reg_write(p, REG_RDT, 0);
    /* Puffer 2 KiB, Kopfpuffer 256 Bytes (unbenutzt), neues Deskriptorformat */
    reg_write(p, REG_SRRCTL, 2 | (4u << 8) | SRRCTL_ONE_BUFFER);
    reg_write(p, REG_RXDCTL, QUEUE_ENABLE | 8 | (8u << 8) | (1u << 16));
    reg_wait(p, REG_RXDCTL, QUEUE_ENABLE, QUEUE_ENABLE, 50);
    reg_write(p, REG_RDT, RX_COUNT - 1);
    reg_write(p, REG_RCTL, RCTL_ENABLE | RCTL_BROADCAST | RCTL_STRIP_CRC);

    /* Senden */
    reg_write(p, REG_TXDCTL, 0);
    reg_read(p, REG_STATUS);
    reg_write(p, REG_TDLEN, TX_COUNT * sizeof(tx_desc_t));
    reg_write(p, REG_TDBAL, (uint32_t)(uint64_t)p->tx);
    reg_write(p, REG_TDBAH, (uint32_t)((uint64_t)p->tx >> 32));
    reg_write(p, REG_TDH, 0);
    reg_write(p, REG_TDT, 0);
    reg_write(p, REG_TXDCTL, QUEUE_ENABLE | 8 | (1u << 8));
    reg_wait(p, REG_TXDCTL, QUEUE_ENABLE, QUEUE_ENABLE, 50);
    reg_write(p, REG_TCTL, TCTL_ENABLE | TCTL_PAD | TCTL_RETRY_LATE | (15u << 4) | (63u << 12));

    p->rx_next = 0;
    p->tx_next = 0;
    p->ok = true;
    return true;
}

static bool port_link(port_t *p)
{
    return p->ok && (reg_read(p, REG_STATUS) & STATUS_LINK);
}

static void describe(port_t *p, char *out, size_t size)
{
    uint32_t status = reg_read(p, REG_STATUS);
    const char *speed = status & STATUS_SPEED_2500 ? "2,5 Gbit/s"
                      : status & STATUS_SPEED_1000 ? "1 Gbit/s"
                      : status & STATUS_SPEED_100 ? "100 Mbit/s" : "10 Mbit/s";
    ksnprintf(out, size, "%s", speed);
}

/*
 * Steht die Verbindung? Hat der benutzte Anschluss kein Kabel, ein anderer
 * aber schon, wird dorthin gewechselt - gemeldet wird das als kurze
 * Unterbrechung, damit die Netzwerkschicht sich eine neue Adresse holt.
 */
static bool igc_link_up(nic_t *self)
{
    if (port_link(&ports[active]))
        return true;
    for (int i = 0; i < port_count; i++) {
        if (i != active && port_link(&ports[i])) {
            char speed[16];
            active = i;
            memcpy(self->mac, ports[i].mac, 6);
            describe(&ports[i], speed, sizeof(speed));
            klog("Netzwerk: benutze Anschluss %d (%s)\n", i + 1, speed);
            return false;
        }
    }
    return false;
}

static bool igc_send(nic_t *self, const void *frame, size_t len)
{
    (void)self;
    port_t *p = &ports[active];
    if (!p->ok || len == 0 || len > ETH_FRAME_MAX)
        return false;

    uint64_t flags = irq_save();
    uint32_t slot = p->tx_next;
    uint32_t next = (slot + 1) % TX_COUNT;
    if (next == reg_read(p, REG_TDH) % TX_COUNT) {      /* Ring voll: die Karte kommt nicht nach */
        irq_restore(flags);
        return false;
    }
    uint8_t *buffer = p->tx_buffers + (size_t)slot * BUFFER_SIZE;
    memcpy(buffer, frame, len);
    p->tx[slot].address = (uint64_t)buffer;
    p->tx[slot].command = (uint32_t)len | TX_DATA | TX_EXTENDED | TX_ADD_CRC | TX_END | TX_REPORT;
    p->tx[slot].status = (uint32_t)len << 14;
    p->tx_next = next;
    reg_write(p, REG_TDT, next);
    irq_restore(flags);
    return true;
}

static size_t igc_receive(nic_t *self, void *buffer, size_t max)
{
    (void)self;
    port_t *p = &ports[active];
    if (!p->ok)
        return 0;

    for (;;) {
        rx_desc_t *d = &p->rx[p->rx_next];
        if (!(d->status & RX_DONE))
            return 0;
        size_t len = d->length;
        bool whole = (d->status & RX_END) && !(d->status & 0x80000000u);
        if (whole && len <= max && len <= BUFFER_SIZE)
            memcpy(buffer, p->rx_buffers + (size_t)p->rx_next * BUFFER_SIZE, len);
        else
            len = 0;                    /* zu groß, zerteilt oder fehlerhaft: verwerfen */

        /* Den Deskriptor wieder herrichten und der Karte zurückgeben */
        d->address = (uint64_t)(p->rx_buffers + (size_t)p->rx_next * BUFFER_SIZE);
        d->status = 0;
        d->length = 0;
        d->vlan = 0;
        reg_write(p, REG_RDT, p->rx_next);
        p->rx_next = (p->rx_next + 1) % RX_COUNT;
        if (len)
            return len;
    }
}

/*
 * Die Karte zählt selbst mit, was wirklich über die Leitung ging. Das zeigt,
 * ob Pakete hinausgehen und ankommen - unabhängig von unserer Buchführung.
 * (Die Zähler löschen sich beim Lesen, deshalb summieren wir auf.)
 */
static void igc_stats(nic_t *self, char *text, size_t size)
{
    (void)self;
    port_t *p = &ports[active];
    if (!p->ok) {
        strlcpy(text, "", size);
        return;
    }
    uint64_t flags = irq_save();
    p->sent += reg_read(p, REG_GPTC);
    p->received += reg_read(p, REG_GPRC);
    p->missed += reg_read(p, REG_MPC) + reg_read(p, REG_RNBC);
    p->bad += reg_read(p, REG_CRCERRS) + reg_read(p, REG_RXERRC);
    uint32_t head = reg_read(p, REG_TDH), tail = reg_read(p, REG_TDT);
    irq_restore(flags);
    int used = ksnprintf(text, size,
              "%u gesendet, %u empfangen, %u verpasst, %u fehlerhaft, Sendering %u/%u",
              p->sent, p->received, p->missed, p->bad, head, tail);

    /* Zweite Zeile: der Zustand der Karte in Zahlen, für die Fehlersuche */
    if (used < 0 || (size_t)used + 2 >= size)
        return;
    pci_device_t bridge;
    uint16_t bridge_command = 0xFFFF;
    if (pci_parent_bridge(&p->pci, &bridge))
        bridge_command = pci_read16(&bridge, 0x04);
    ksnprintf(text + used, size - (size_t)used,
              "\nRegister: CTRL %08x STATUS %08x TCTL %08x TXDCTL %08x RCTL %08x "
              "RXDCTL %08x RDH %u RDT %u PCI %04x Bruecke %04x",
              reg_read(p, REG_CTRL), reg_read(p, REG_STATUS), reg_read(p, REG_TCTL),
              reg_read(p, REG_TXDCTL), reg_read(p, REG_RCTL), reg_read(p, REG_RXDCTL),
              reg_read(p, REG_RDH), reg_read(p, REG_RDT),
              pci_read16(&p->pci, 0x04), bridge_command);
}

nic_t *igc_probe(void)
{
    pci_device_t found[8];
    int count = pci_find(0x02, 0x00, 0x00, found, 8);
    bool modern = false;

    for (int i = 0; i < count && port_count < PORTS_MAX; i++) {
        if (found[i].vendor_id != 0x8086)
            continue;
        for (unsigned k = 0; k < sizeof(device_ids) / sizeof(device_ids[0]); k++) {
            if (found[i].device_id != device_ids[k].device)
                continue;
            port_t *p = &ports[port_count];
            memset(p, 0, sizeof(*p));
            p->modern = device_ids[k].modern;
            if (start_port(p, &found[i])) {
                port_count++;
                modern = modern || p->modern;
            } else {
                klog("Netzwerk: Anschluss %04x:%04x ließ sich nicht einrichten\n",
                     found[i].vendor_id, found[i].device_id);
            }
            break;
        }
    }
    if (port_count == 0)
        return NULL;

    /* Fürs Erste den ersten Anschluss; steckt das Kabel woanders, wechselt igc_link_up(). */
    active = 0;
    nic.name = modern ? (port_count > 1 ? "Intel I225/I226 (mehrere Anschlüsse)"
                                        : "Intel I225/I226")
                      : "Intel 82576/I210";
    memcpy(nic.mac, ports[0].mac, 6);
    nic.link_up = igc_link_up;
    nic.send = igc_send;
    nic.receive = igc_receive;
    nic.stats = igc_stats;
    nic.driver_data = ports;
    return &nic;
}
