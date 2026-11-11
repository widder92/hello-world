/*
 * Treiber für Intel-Gigabit-Netzwerkkarten der e1000-Familie.
 *
 * Diese Karten stecken in vielen PCs (als "Intel Ethernet Connection") und
 * werden von QEMU, VirtualBox und VMware emuliert. Der Treiber arbeitet ohne
 * Interrupts: Der Netzwerk-Task fragt regelmäßig nach neuen Paketen.
 *
 * Senden und Empfangen laufen über zwei Ringe aus "Deskriptoren". Jeder
 * Deskriptor zeigt auf einen Puffer; Karte und Treiber reichen sich die
 * Deskriptoren über Kopf- und Endezeiger gegenseitig zu.
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
#define REG_EERD    0x0014
#define REG_IMC     0x00D8
#define REG_RCTL    0x0100
#define REG_TCTL    0x0400
#define REG_TIPG    0x0410
#define REG_RDBAL   0x2800
#define REG_RDBAH   0x2804
#define REG_RDLEN   0x2808
#define REG_RDH     0x2810
#define REG_RDT     0x2818
#define REG_TDBAL   0x3800
#define REG_TDBAH   0x3804
#define REG_TDLEN   0x3808
#define REG_TDH     0x3810
#define REG_TDT     0x3818
#define REG_MTA     0x5200
#define REG_RAL     0x5400
#define REG_RAH     0x5404

#define CTRL_ASDE   (1u << 5)           /* Geschwindigkeit automatisch */
#define CTRL_SLU    (1u << 6)           /* Verbindung aufbauen */
#define CTRL_RESET  (1u << 26)
#define STATUS_LINK (1u << 1)

#define RCTL_ENABLE    (1u << 1)
#define RCTL_BROADCAST (1u << 15)
#define RCTL_STRIP_CRC (1u << 26)
#define TCTL_ENABLE    (1u << 1)
#define TCTL_PAD       (1u << 3)

#define RX_COUNT    256
#define TX_COUNT    64
#define BUFFER_SIZE 2048

#define DESC_DONE   0x01                /* Statusbit: Karte ist fertig */
#define TX_CMD_EOP  0x01                /* Ende des Pakets */
#define TX_CMD_FCS  0x02                /* Prüfsumme anhängen */
#define TX_CMD_RS   0x08                /* Status zurückmelden */

struct rx_desc {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct tx_desc {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} __attribute__((packed));

typedef struct {
    uint8_t *mmio;
    struct rx_desc *rx;
    struct tx_desc *tx;
    uint8_t *rx_buffers, *tx_buffers;
    unsigned rx_next, tx_next;
} e1000_t;

/* Unterstützte Karten (Hersteller ist immer Intel, 0x8086) */
static const uint16_t device_ids[] = {
    0x100E, 0x100F, 0x1004, 0x1010, 0x1011, 0x1026, 0x1027, 0x1028, /* 8254x */
    0x10D3, 0x10F6, 0x150C,                                 /* 82574, 82583 */
    0x1502, 0x1503,                                         /* 82579 */
    0x153A, 0x153B, 0x155A, 0x1559, 0x15A0, 0x15A1, 0x15A2, 0x15A3, /* I217/I218 */
    0x156F, 0x1570, 0x15B7, 0x15B8, 0x15B9, 0x15D7, 0x15D8, 0x15E3, /* I219 */
    0x15D6, 0x15BD, 0x15BE, 0x15BB, 0x15BC, 0x15DF, 0x15E0, 0x15E1,
    0x15E2, 0x0D4E, 0x0D4F, 0x0D4C, 0x0D4D, 0x0D53, 0x0D55, 0x15FB,
    0x15FC, 0x15F9, 0x15FA, 0x15F4, 0x15F5, 0x1A1E, 0x1A1F, 0x1A1C,
    0x1A1D, 0x550A, 0x550B, 0x550C, 0x550D,
};

static uint32_t reg_read(const e1000_t *e, uint32_t reg)
{
    return *(volatile uint32_t *)(e->mmio + reg);
}

static void reg_write(e1000_t *e, uint32_t reg, uint32_t value)
{
    *(volatile uint32_t *)(e->mmio + reg) = value;
}

static bool e1000_link_up(nic_t *nic)
{
    return reg_read(nic->driver_data, REG_STATUS) & STATUS_LINK;
}

static bool e1000_send(nic_t *nic, const void *frame, size_t len)
{
    e1000_t *e = nic->driver_data;
    if (len > ETH_FRAME_MAX)
        return false;

    struct tx_desc *desc = &e->tx[e->tx_next];
    if (!(desc->status & DESC_DONE))
        return false;                   /* Ring voll: Paket verwerfen */

    memcpy(e->tx_buffers + e->tx_next * BUFFER_SIZE, frame, len);
    desc->length = (uint16_t)len;
    desc->command = TX_CMD_EOP | TX_CMD_FCS | TX_CMD_RS;
    desc->status = 0;

    e->tx_next = (e->tx_next + 1) % TX_COUNT;
    reg_write(e, REG_TDT, e->tx_next);  /* der Karte Bescheid geben */
    return true;
}

static size_t e1000_receive(nic_t *nic, void *buffer, size_t max)
{
    e1000_t *e = nic->driver_data;
    struct rx_desc *desc = &e->rx[e->rx_next];
    if (!(desc->status & DESC_DONE))
        return 0;

    size_t len = desc->length;
    bool ok = (desc->status & 0x02) && !desc->errors && len <= max;
    if (ok)
        memcpy(buffer, e->rx_buffers + e->rx_next * BUFFER_SIZE, len);

    /* Deskriptor zurückgeben, damit die Karte ihn wieder füllen kann */
    desc->status = 0;
    reg_write(e, REG_RDT, e->rx_next);
    e->rx_next = (e->rx_next + 1) % RX_COUNT;
    return ok ? len : 0;
}

/* Liest ein Wort aus dem Konfigurationsspeicher der Karte (für die MAC). */
static bool eeprom_read(e1000_t *e, uint8_t address, uint16_t *out)
{
    /* Zwei Bauarten des Registers: ältere und neuere Karten */
    static const struct {
        uint32_t shift, done;
    } variants[] = { { 8, 1u << 4 }, { 2, 1u << 1 } };

    for (int v = 0; v < 2; v++) {
        reg_write(e, REG_EERD, ((uint32_t)address << variants[v].shift) | 1);
        for (int i = 0; i < 2000; i++) {
            uint32_t value = reg_read(e, REG_EERD);
            if (value & variants[v].done) {
                *out = (uint16_t)(value >> 16);
                return true;
            }
            time_delay_us(10);
        }
    }
    return false;
}

static void read_mac(e1000_t *e, uint8_t mac[6])
{
    uint32_t low = reg_read(e, REG_RAL);
    uint32_t high = reg_read(e, REG_RAH);
    if (low || (high & 0xFFFF)) {       /* schon von der Firmware eingetragen */
        memcpy(mac, &low, 4);
        memcpy(mac + 4, &high, 2);
        return;
    }
    for (int i = 0; i < 3; i++) {
        uint16_t word = 0;
        eeprom_read(e, (uint8_t)i, &word);
        mac[2 * i] = (uint8_t)word;
        mac[2 * i + 1] = (uint8_t)(word >> 8);
    }
}

nic_t *e1000_probe(void)
{
    pci_device_t found[8];
    int count = pci_find(0x02, 0x00, 0x00, found, 8);
    const pci_device_t *pci = NULL;
    for (int i = 0; i < count && !pci; i++) {
        for (unsigned k = 0; k < sizeof(device_ids) / sizeof(device_ids[0]); k++) {
            if (found[i].vendor_id == 0x8086 && found[i].device_id == device_ids[k])
                pci = &found[i];
        }
    }
    if (!pci)
        return NULL;

    uint64_t bar = pci_read32(pci, 0x10);
    if ((bar & 0x6) == 0x4)
        bar |= (uint64_t)pci_read32(pci, 0x14) << 32;
    bar &= ~0xFull;
    if (!bar)
        return NULL;
    pci_enable(pci);
    vmm_map(bar, 0x20000);

    static e1000_t device;
    static nic_t nic;
    e1000_t *e = &device;
    e->mmio = (uint8_t *)bar;

    /* Karte zurücksetzen; Interrupts bleiben aus */
    reg_write(e, REG_IMC, 0xFFFFFFFF);
    uint8_t mac[6];
    read_mac(e, mac);                   /* vor dem Reset merken */
    reg_write(e, REG_CTRL, reg_read(e, REG_CTRL) | CTRL_RESET);
    time_delay_ms(20);
    reg_write(e, REG_IMC, 0xFFFFFFFF);

    uint32_t ctrl = reg_read(e, REG_CTRL);
    ctrl |= CTRL_SLU | CTRL_ASDE;
    ctrl &= ~((1u << 3) | (1u << 7) | (1u << 30) | (1u << 31));
    reg_write(e, REG_CTRL, ctrl);

    uint8_t after[6];
    read_mac(e, after);
    if (after[0] | after[1] | after[2] | after[3] | after[4] | after[5])
        memcpy(mac, after, 6);
    uint32_t low, high = 0;
    memcpy(&low, mac, 4);
    memcpy(&high, mac + 4, 2);
    reg_write(e, REG_RAL, low);
    reg_write(e, REG_RAH, high | (1u << 31));    /* Adresse gültig */
    for (int i = 0; i < 128; i++)
        reg_write(e, REG_MTA + 4 * i, 0);

    /* Empfangsring: alle Deskriptoren gehören zunächst der Karte */
    e->rx = alloc_pages(1);
    e->rx_buffers = alloc_pages(RX_COUNT * BUFFER_SIZE / 4096);
    for (int i = 0; i < RX_COUNT; i++)
        e->rx[i].address = (uint64_t)(e->rx_buffers + i * BUFFER_SIZE);
    reg_write(e, REG_RDBAL, (uint32_t)(uint64_t)e->rx);
    reg_write(e, REG_RDBAH, (uint32_t)((uint64_t)e->rx >> 32));
    reg_write(e, REG_RDLEN, RX_COUNT * sizeof(struct rx_desc));
    reg_write(e, REG_RDH, 0);
    reg_write(e, REG_RDT, RX_COUNT - 1);
    reg_write(e, REG_RCTL, RCTL_ENABLE | RCTL_BROADCAST | RCTL_STRIP_CRC);

    /* Sendering: alle Deskriptoren sind frei ("fertig") */
    e->tx = alloc_pages(1);
    e->tx_buffers = alloc_pages(TX_COUNT * BUFFER_SIZE / 4096);
    for (int i = 0; i < TX_COUNT; i++) {
        e->tx[i].address = (uint64_t)(e->tx_buffers + i * BUFFER_SIZE);
        e->tx[i].status = DESC_DONE;
    }
    reg_write(e, REG_TDBAL, (uint32_t)(uint64_t)e->tx);
    reg_write(e, REG_TDBAH, (uint32_t)((uint64_t)e->tx >> 32));
    reg_write(e, REG_TDLEN, TX_COUNT * sizeof(struct tx_desc));
    reg_write(e, REG_TDH, 0);
    reg_write(e, REG_TDT, 0);
    reg_write(e, REG_TIPG, 0x0060200A);
    reg_write(e, REG_TCTL, TCTL_ENABLE | TCTL_PAD | (0x10u << 4) | (0x40u << 12));

    nic.name = "Intel e1000";
    memcpy(nic.mac, mac, 6);
    nic.link_up = e1000_link_up;
    nic.send = e1000_send;
    nic.receive = e1000_receive;
    nic.driver_data = e;
    return &nic;
}
