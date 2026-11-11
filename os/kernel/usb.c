/*
 * USB-Unterstützung für Tastaturen und Mäuse an xHCI-Controllern.
 *
 * xHCI ist der Controller-Standard für USB 3 und steckt in praktisch jedem
 * PC seit etwa 2012; er bedient auch langsame USB-1/2-Geräte. Der Treiber
 * beschränkt sich auf das Nötigste:
 *
 *   - Geräte direkt an den Anschlüssen des Rechners (keine USB-Hubs)
 *   - Tastaturen und Mäuse im einfachen "Boot-Protokoll" (HID)
 *   - Abfragen statt Interrupts: Jeder Controller hat einen eigenen Task,
 *     der regelmäßig nachsieht, ob es Neuigkeiten gibt
 *
 * Ablauf: Controller zurücksetzen und starten -> Anschlüsse beobachten ->
 * neues Gerät: Adresse vergeben, Beschreibung lesen, Endpunkt einrichten ->
 * danach liefert das Gerät bei jeder Eingabe einen kleinen Bericht.
 */
#include "usb.h"
#include "console.h"
#include "gui.h"
#include "input.h"
#include "memory.h"
#include "cpu.h"
#include "pci.h"
#include "serial.h"
#include "string.h"
#include "task.h"
#include "time.h"

#define MAX_CONTROLLERS 4
#define MAX_DEVICES     8
#define MAX_PORTS       64
#define RING_TRBS       256             /* eine Seite voller TRBs */
#define PAGE_SIZE       4096

/* --- Register ------------------------------------------------------------------- */

/* Capability-Register */
#define CAP_CAPLENGTH   0x00
#define CAP_HCSPARAMS1  0x04
#define CAP_HCSPARAMS2  0x08
#define CAP_HCCPARAMS1  0x10
#define CAP_DBOFF       0x14
#define CAP_RTSOFF      0x18

/* Operational-Register */
#define OP_USBCMD       0x00
#define OP_USBSTS       0x04
#define OP_PAGESIZE     0x08
#define OP_CRCR         0x18
#define OP_DCBAAP       0x30
#define OP_CONFIG       0x38
#define OP_PORTSC(n)    (0x400 + 0x10 * (n))

#define CMD_RUN         (1u << 0)
#define CMD_RESET       (1u << 1)
#define STS_HALTED      (1u << 0)
#define STS_NOT_READY   (1u << 11)

#define PORT_CONNECTED  (1u << 0)
#define PORT_ENABLED    (1u << 1)
#define PORT_RESET      (1u << 4)
#define PORT_POWER      (1u << 9)
#define PORT_SPEED(v)   (((v) >> 10) & 0xF)
#define PORT_CONNECT_CHANGE (1u << 17)
#define PORT_RESET_CHANGE   (1u << 21)
#define PORT_CHANGE_BITS    0x00FE0000u
/* Bits, die beim Zurückschreiben erhalten bleiben sollen (alle anderen sind
 * nur lesbar oder lösen beim Schreiben einer 1 etwas aus). */
#define PORT_KEEP_BITS      (PORT_POWER | 0x0000C000u | 0x0E000000u)

/* Register der ersten "Interrupter"-Einheit (wir fragen sie nur ab) */
#define IR_ERSTSZ       0x28
#define IR_ERSTBA       0x30
#define IR_ERDP         0x38

enum { SPEED_FULL = 1, SPEED_LOW = 2, SPEED_HIGH = 3, SPEED_SUPER = 4 };

/* --- TRBs: die Nachrichten zwischen Treiber und Controller ---------------------- */

typedef struct {
    uint64_t param;
    uint32_t status;
    uint32_t control;
} trb_t;

#define TRB_TYPE(t)     ((uint32_t)(t) << 10)
#define TRB_GET_TYPE(c) (((c) >> 10) & 0x3F)
#define TRB_CYCLE       (1u << 0)
#define TRB_TOGGLE      (1u << 1)       /* Link-TRB: Cycle-Bit umschalten */
#define TRB_ISP         (1u << 2)       /* auch bei kurzen Paketen melden */
#define TRB_IOC         (1u << 5)       /* Ereignis nach Abschluss */
#define TRB_IDT         (1u << 6)       /* Daten stehen direkt im TRB */
#define TRB_DIR_IN      (1u << 16)

enum {
    TRB_NORMAL = 1,
    TRB_SETUP = 2,
    TRB_DATA = 3,
    TRB_STATUS = 4,
    TRB_LINK = 6,
    TRB_ENABLE_SLOT = 9,
    TRB_DISABLE_SLOT = 10,
    TRB_ADDRESS_DEVICE = 11,
    TRB_CONFIGURE_ENDPOINT = 12,
    TRB_EVALUATE_CONTEXT = 13,
    TRB_RESET_ENDPOINT = 14,
    TRB_SET_DEQUEUE = 16,
    TRB_EVENT_TRANSFER = 32,
    TRB_EVENT_COMMAND = 33,
};

#define CC_SUCCESS      1
#define CC_SHORT_PACKET 13

/* Ein Ring aus TRBs: Der Treiber schreibt, der Controller arbeitet ab. */
typedef struct {
    trb_t *trbs;
    unsigned enqueue;
    uint32_t cycle;
} ring_t;

/* --- Geräte ---------------------------------------------------------------------- */

typedef enum { HID_NONE, HID_KEYBOARD, HID_MOUSE } hid_kind_t;

typedef struct {
    hid_kind_t kind;
    uint8_t interface;
    uint8_t dci;                        /* Nummer des Endpunkts im Controller */
    uint8_t interval;
    uint16_t max_packet;
    ring_t ring;
    uint8_t *buffer;
    uint8_t previous[8];                /* Tastatur: letzter Bericht */
} hid_t;

typedef struct {
    bool used;
    uint8_t slot;
    uint8_t port;                       /* ab 0 gezählt */
    uint8_t speed;
    uint32_t *out_ctx;                  /* vom Controller gepflegt */
    uint32_t *in_ctx;                   /* unsere Anweisungen an den Controller */
    ring_t ep0;
    uint8_t *buffer;                    /* für Steuertransfers */
    hid_t hid[2];
} device_t;

typedef struct {
    pci_device_t pci;
    int number;
    uint8_t *cap, *op, *runtime;
    volatile uint32_t *doorbell;
    int ports, context_size;
    uint64_t *dcbaa;                    /* Zeiger auf die Gerätekontexte */
    ring_t commands;
    trb_t *events;
    unsigned event_dequeue;
    uint32_t event_cycle;

    /* Worauf der Treiber gerade wartet */
    trb_t *pending_command;
    bool command_done;
    uint32_t command_code, command_slot;
    uint32_t control_slot;
    bool control_done;
    uint32_t control_code;

    device_t devices[MAX_DEVICES];
    uint8_t failures[MAX_PORTS];        /* Fehlversuche je Anschluss */

    /* Tastenwiederholung (USB-Tastaturen wiederholen nicht selbst) */
    uint16_t repeat_key;
    uint64_t repeat_at;
} xhci_t;

/* --- Hilfsfunktionen -------------------------------------------------------------- */

static uint32_t rd32(const uint8_t *base, uint32_t offset)
{
    return *(volatile const uint32_t *)(base + offset);
}

static void wr32(uint8_t *base, uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(base + offset) = value;
}

static void wr64(uint8_t *base, uint32_t offset, uint64_t value)
{
    wr32(base, offset, (uint32_t)value);
    wr32(base, offset + 4, (uint32_t)(value >> 32));
}

/* Wartet, bis (Register & mask) == value gilt. */
static bool wait_bits(const uint8_t *base, uint32_t offset, uint32_t mask,
                      uint32_t value, uint32_t timeout_ms)
{
    uint64_t deadline = time_ms() + timeout_ms;
    while ((rd32(base, offset) & mask) != value) {
        if (time_ms() > deadline)
            return false;
        task_sleep(1);
    }
    return true;
}

/* Einzelne Seiten für Controller-Strukturen, mit Wiederverwendung. */
static void *free_pages;

static void *page_get(void)
{
    uint64_t flags = irq_save();
    void *page = free_pages;
    if (page)
        free_pages = *(void **)page;
    irq_restore(flags);

    if (!page)
        return alloc_pages(1);
    memset(page, 0, PAGE_SIZE);
    return page;
}

static void page_put(void *page)
{
    if (!page)
        return;
    uint64_t flags = irq_save();
    *(void **)page = free_pages;
    free_pages = page;
    irq_restore(flags);
}

static void ring_init(ring_t *ring)
{
    ring->trbs = page_get();
    ring->enqueue = 0;
    ring->cycle = 1;
}

static trb_t *ring_push(ring_t *ring, uint64_t param, uint32_t status,
                        uint32_t control)
{
    trb_t *trb = &ring->trbs[ring->enqueue];
    trb->param = param;
    trb->status = status;
    trb->control = control | ring->cycle;

    /* Am Ende verweist ein Link-TRB zurück auf den Anfang. */
    if (++ring->enqueue == RING_TRBS - 1) {
        trb_t *link = &ring->trbs[RING_TRBS - 1];
        link->param = (uint64_t)ring->trbs;
        link->status = 0;
        link->control = TRB_TYPE(TRB_LINK) | TRB_TOGGLE | ring->cycle;
        ring->cycle ^= 1;
        ring->enqueue = 0;
    }
    return trb;
}

/* Die Kontexte sind je nach Controller 32 oder 64 Bytes groß. */
static uint32_t *context(const xhci_t *x, uint32_t *base, int index)
{
    return (uint32_t *)((uint8_t *)base + index * x->context_size);
}

/* --- Eingaben verarbeiten ----------------------------------------------------------- */

/* USB-Tastencode -> Scancode (Set 1); 0x100 markiert das Präfix 0xE0. */
static const uint16_t usage_to_scancode[0x65] = {
    [0x04] = 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24,   /* a-j */
    [0x0E] = 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14,   /* k-t */
    [0x18] = 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,                           /* u-z */
    [0x1E] = 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,   /* 1-0 */
    [0x28] = 0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A, 0x1B, 0x2B,
    [0x32] = 0x2B, 0x27, 0x28, 0x29, 0x33, 0x34, 0x35, 0x3A,
    [0x3A] = 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,   /* F1-F10 */
    [0x44] = 0x57, 0x58,                                                   /* F11, F12 */
    [0x49] = 0x152, 0x147, 0x149, 0x153, 0x14F, 0x151,  /* Einfg ... Bild ab */
    [0x4F] = 0x14D, 0x14B, 0x150, 0x148,                /* Pfeiltasten */
    [0x54] = 0x135, 0x37, 0x4A, 0x4E, 0x11C,            /* Ziffernblock */
    [0x59] = 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x52, 0x53,
    [0x64] = 0x56,
};

/* Umschalttasten in der Reihenfolge der Bits im Tastaturbericht */
static const uint16_t modifier_scancode[8] = {
    0x1D, 0x2A, 0x38, 0, 0x11D, 0x36, 0x138, 0,
};

static void send_key(uint16_t code, bool released)
{
    if (code)
        keyboard_handle_scancode(code & 0xFF, code & 0x100, released);
}

static bool contains(const uint8_t *keys, uint8_t key)
{
    for (int i = 2; i < 8; i++) {
        if (keys[i] == key)
            return true;
    }
    return false;
}

/* Der Bericht enthält alle gerade gedrückten Tasten: mit dem letzten vergleichen. */
static void keyboard_report(xhci_t *x, hid_t *hid, const uint8_t *report)
{
    if (report[2] == 0x01)
        return;                         /* zu viele Tasten gleichzeitig */

    uint8_t changed = report[0] ^ hid->previous[0];
    for (int bit = 0; bit < 8; bit++) {
        if (changed & (1 << bit))
            send_key(modifier_scancode[bit], !(report[0] & (1 << bit)));
    }
    for (int i = 2; i < 8; i++) {
        uint8_t key = hid->previous[i];
        if (key >= 4 && key < 0x65 && !contains(report, key)) {
            send_key(usage_to_scancode[key], true);
            if (x->repeat_key == usage_to_scancode[key])
                x->repeat_key = 0;
        }
    }
    for (int i = 2; i < 8; i++) {
        uint8_t key = report[i];
        if (key >= 4 && key < 0x65 && !contains(hid->previous, key)) {
            send_key(usage_to_scancode[key], false);
            if (key != 0x39) {          /* Feststelltaste nicht wiederholen */
                x->repeat_key = usage_to_scancode[key];
                x->repeat_at = time_ms() + 400;
            }
        }
    }
    memcpy(hid->previous, report, 8);
}

static void hid_report(xhci_t *x, hid_t *hid, uint32_t length)
{
    const uint8_t *report = hid->buffer;
    if (hid->kind == HID_KEYBOARD && length >= 8)
        keyboard_report(x, hid, report);
    else if (hid->kind == HID_MOUSE && length >= 3)
        gui_post_mouse((int8_t)report[1], (int8_t)report[2], report[0] & 0x07);
}

/* Bittet das Gerät um den nächsten Bericht. */
static void hid_request(xhci_t *x, device_t *dev, hid_t *hid)
{
    ring_push(&hid->ring, (uint64_t)hid->buffer, hid->max_packet,
              TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    x->doorbell[dev->slot] = hid->dci;
}

/* --- Ereignisse des Controllers ------------------------------------------------------ */

static device_t *device_by_slot(xhci_t *x, uint32_t slot)
{
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (x->devices[i].used && x->devices[i].slot == slot)
            return &x->devices[i];
    }
    return NULL;
}

static void handle_event(xhci_t *x, const trb_t *ev)
{
    uint32_t type = TRB_GET_TYPE(ev->control);
    uint32_t code = ev->status >> 24;
    uint32_t slot = ev->control >> 24;

    if (type == TRB_EVENT_COMMAND) {
        if (ev->param == (uint64_t)x->pending_command) {
            x->command_code = code;
            x->command_slot = slot;
            x->command_done = true;
        }
        return;
    }
    if (type != TRB_EVENT_TRANSFER)
        return;

    uint32_t dci = (ev->control >> 16) & 0x1F;
    if (dci == 1) {                     /* Steuertransfer abgeschlossen */
        if (slot == x->control_slot) {
            x->control_code = code;
            x->control_done = true;
        }
        return;
    }

    device_t *dev = device_by_slot(x, slot);
    if (!dev)
        return;
    for (int i = 0; i < 2; i++) {
        hid_t *hid = &dev->hid[i];
        if (hid->kind == HID_NONE || hid->dci != dci)
            continue;
        if (code == CC_SUCCESS || code == CC_SHORT_PACKET) {
            uint32_t residual = ev->status & 0xFFFFFF;
            hid_report(x, hid, hid->max_packet - residual);
            hid_request(x, dev, hid);
        }
        /* Bei Fehlern fragen wir nicht weiter - meist wurde abgesteckt. */
    }
}

static void poll_events(xhci_t *x)
{
    bool any = false;
    for (;;) {
        const trb_t *ev = &x->events[x->event_dequeue];
        if ((ev->control & TRB_CYCLE) != x->event_cycle)
            break;
        handle_event(x, ev);
        if (++x->event_dequeue == RING_TRBS) {
            x->event_dequeue = 0;
            x->event_cycle ^= 1;
        }
        any = true;
    }
    if (any) {
        /* Dem Controller sagen, bis wohin wir gelesen haben (Bit 3: erledigt) */
        wr64(x->runtime, IR_ERDP,
             (uint64_t)&x->events[x->event_dequeue] | (1u << 3));
    }
}

/* Schickt ein Kommando an den Controller und wartet auf dessen Antwort. */
static bool command(xhci_t *x, uint64_t param, uint32_t control)
{
    x->command_done = false;
    x->pending_command = ring_push(&x->commands, param, 0, control);
    x->doorbell[0] = 0;

    uint64_t deadline = time_ms() + 2000;
    while (!x->command_done) {
        poll_events(x);
        if (x->command_done)
            break;
        if (time_ms() > deadline) {
            x->pending_command = NULL;
            return false;
        }
        task_sleep(1);
    }
    x->pending_command = NULL;
    return x->command_code == CC_SUCCESS;
}

/*
 * Steuertransfer: eine Anfrage an das Gerät, optional mit Daten (höchstens
 * eine Seite) in dev->buffer. Richtung laut Bit 7 von request_type.
 */
static bool control(xhci_t *x, device_t *dev, uint8_t request_type,
                    uint8_t request, uint16_t value, uint16_t index,
                    uint16_t length)
{
    bool in = request_type & 0x80;
    uint64_t setup = (uint64_t)request_type | ((uint64_t)request << 8) |
                     ((uint64_t)value << 16) | ((uint64_t)index << 32) |
                     ((uint64_t)length << 48);
    uint32_t transfer_type = length == 0 ? 0 : (in ? 3 : 2);

    x->control_done = false;
    x->control_slot = dev->slot;
    ring_push(&dev->ep0, setup, 8,
              TRB_TYPE(TRB_SETUP) | TRB_IDT | (transfer_type << 16));
    if (length)
        ring_push(&dev->ep0, (uint64_t)dev->buffer, length,
                  TRB_TYPE(TRB_DATA) | (in ? TRB_DIR_IN : 0));
    ring_push(&dev->ep0, 0, 0,
              TRB_TYPE(TRB_STATUS) | TRB_IOC |
                  ((length && in) ? 0 : TRB_DIR_IN));
    x->doorbell[dev->slot] = 1;

    uint64_t deadline = time_ms() + 2000;
    while (!x->control_done) {
        poll_events(x);
        if (x->control_done)
            break;
        if (time_ms() > deadline)
            break;
        task_sleep(1);
    }
    x->control_slot = 0;
    if (x->control_done && x->control_code == CC_SUCCESS)
        return true;

    /*
     * Fehlgeschlagen (z. B. vom Gerät abgelehnt): Der Endpunkt steht jetzt.
     * Zurücksetzen und hinter den abgebrochenen Transfer springen.
     */
    command(x, 0, TRB_TYPE(TRB_RESET_ENDPOINT) | (1u << 16) |
                      ((uint32_t)dev->slot << 24));
    command(x, (uint64_t)&dev->ep0.trbs[dev->ep0.enqueue] | dev->ep0.cycle,
            TRB_TYPE(TRB_SET_DEQUEUE) | (1u << 16) |
                ((uint32_t)dev->slot << 24));
    return false;
}

/* --- Geräte einrichten und entfernen -------------------------------------------------- */

static void device_free(xhci_t *x, device_t *dev)
{
    if (dev->slot) {
        command(x, 0, TRB_TYPE(TRB_DISABLE_SLOT) | ((uint32_t)dev->slot << 24));
        x->dcbaa[dev->slot] = 0;
    }
    /* Noch gedrückte Tasten loslassen */
    for (int i = 0; i < 2; i++) {
        hid_t *hid = &dev->hid[i];
        if (hid->kind == HID_KEYBOARD) {
            static const uint8_t nothing[8];
            keyboard_report(x, hid, nothing);
        }
        page_put(hid->ring.trbs);
        page_put(hid->buffer);
    }
    page_put(dev->out_ctx);
    page_put(dev->in_ctx);
    page_put(dev->ep0.trbs);
    page_put(dev->buffer);
    memset(dev, 0, sizeof(*dev));
}

/* Sucht in der Gerätebeschreibung nach Tastatur- und Maus-Schnittstellen. */
static int parse_configuration(device_t *dev, uint16_t total, uint8_t speed)
{
    const uint8_t *p = dev->buffer;
    const uint8_t *end = p + total;
    hid_t *current = NULL;
    int found = 0;

    while (p + 2 <= end && p[0] >= 2 && p + p[0] <= end) {
        if (p[1] == 4 && p[0] >= 9) {           /* Schnittstelle */
            current = NULL;
            bool boot_hid = p[5] == 3 && p[6] == 1 && p[3] == 0;
            if (boot_hid && (p[7] == 1 || p[7] == 2) && found < 2) {
                current = &dev->hid[found];
                current->kind = p[7] == 1 ? HID_KEYBOARD : HID_MOUSE;
                current->interface = p[2];
                current->dci = 0;
            }
        } else if (p[1] == 5 && p[0] >= 7 && current && !current->dci) {
            /* Endpunkt: Wir brauchen den, über den Berichte hereinkommen */
            bool in = p[2] & 0x80;
            bool interrupt = (p[3] & 0x03) == 3;
            if (in && interrupt) {
                current->dci = (uint8_t)((p[2] & 0x0F) * 2 + 1);
                current->max_packet = (uint16_t)((p[4] | (p[5] << 8)) & 0x7FF);
                if (current->max_packet > 64)
                    current->max_packet = 64;

                /* Abfrageintervall in die Einheit des Controllers umrechnen */
                unsigned interval = p[6] ? p[6] : 1;
                if (speed == SPEED_LOW || speed == SPEED_FULL) {
                    unsigned exponent = 3;
                    while (exponent < 10 && (8u << (exponent - 2)) <= interval * 8)
                        exponent++;
                    current->interval = (uint8_t)exponent;
                } else {
                    current->interval = (uint8_t)(interval > 16 ? 15 : interval - 1);
                }
                found++;
                current = NULL;
            }
        }
        p += p[0];
    }

    /* Schnittstellen ohne passenden Endpunkt wieder streichen */
    for (int i = 0; i < 2; i++) {
        if (!dev->hid[i].dci)
            dev->hid[i].kind = HID_NONE;
    }
    return found;
}

static bool port_reset(xhci_t *x, int port)
{
    uint32_t status = rd32(x->op, OP_PORTSC(port));
    if (status & PORT_ENABLED)
        return true;                    /* USB 3: schon betriebsbereit */

    wr32(x->op, OP_PORTSC(port), (status & PORT_KEEP_BITS) | PORT_RESET);
    if (!wait_bits(x->op, OP_PORTSC(port), PORT_RESET_CHANGE,
                   PORT_RESET_CHANGE, 500))
        return false;
    status = rd32(x->op, OP_PORTSC(port));
    wr32(x->op, OP_PORTSC(port),
         (status & PORT_KEEP_BITS) | PORT_RESET_CHANGE);
    task_sleep(20);                     /* Erholungszeit nach dem Reset */
    return rd32(x->op, OP_PORTSC(port)) & PORT_ENABLED;
}

/* Liefert NULL bei Erfolg, sonst eine kurze Beschreibung des Problems. */
static const char *device_setup(xhci_t *x, device_t *dev)
{
    static const uint16_t default_packet[] = { 8, 8, 8, 64, 512, 512 };
    uint16_t packet0 = default_packet[dev->speed <= 5 ? dev->speed : 3];

    if (!command(x, 0, TRB_TYPE(TRB_ENABLE_SLOT)))
        return "kein freier Geräteplatz";
    dev->slot = (uint8_t)x->command_slot;

    dev->out_ctx = page_get();
    dev->in_ctx = page_get();
    dev->buffer = page_get();
    ring_init(&dev->ep0);
    x->dcbaa[dev->slot] = (uint64_t)dev->out_ctx;

    /* Eingabekontext: Slot (A0) und Steuer-Endpunkt (A1) einrichten */
    uint32_t *ctrl = context(x, dev->in_ctx, 0);
    uint32_t *slot = context(x, dev->in_ctx, 1);
    uint32_t *ep0 = context(x, dev->in_ctx, 2);
    ctrl[1] = 0x3;
    slot[0] = ((uint32_t)dev->speed << 20) | (1u << 27);
    slot[1] = (uint32_t)(dev->port + 1) << 16;
    ep0[1] = (3u << 1) | (4u << 3) | ((uint32_t)packet0 << 16);
    ep0[2] = (uint32_t)(uint64_t)dev->ep0.trbs | 1;
    ep0[3] = (uint32_t)((uint64_t)dev->ep0.trbs >> 32);
    ep0[4] = 8;

    if (!command(x, (uint64_t)dev->in_ctx,
                 TRB_TYPE(TRB_ADDRESS_DEVICE) | ((uint32_t)dev->slot << 24)))
        return "nimmt keine Adresse an";
    task_sleep(10);                     /* Gerät braucht einen Moment */

    /* Die ersten 8 Bytes der Beschreibung verraten die echte Paketgröße. */
    if (!control(x, dev, 0x80, 6, 0x0100, 0, 8))
        return "Beschreibung nicht lesbar";
    uint16_t real_packet = dev->buffer[7];
    if (dev->speed >= SPEED_SUPER)
        real_packet = (uint16_t)(1u << (real_packet > 12 ? 9 : real_packet));
    if (real_packet >= 8 && real_packet != packet0) {
        ctrl[1] = 0x2;
        ep0[1] = (3u << 1) | (4u << 3) | ((uint32_t)real_packet << 16);
        if (!command(x, (uint64_t)dev->in_ctx,
                     TRB_TYPE(TRB_EVALUATE_CONTEXT) |
                         ((uint32_t)dev->slot << 24)))
            return "Paketgröße nicht einstellbar";
    }

    /* Konfiguration lesen: erst den Kopf (mit der Gesamtlänge), dann alles */
    if (!control(x, dev, 0x80, 6, 0x0200, 0, 9))
        return "Konfiguration nicht lesbar";
    uint16_t total = (uint16_t)(dev->buffer[2] | (dev->buffer[3] << 8));
    uint8_t config_value = dev->buffer[5];
    if (total > 1024)
        total = 1024;
    if (!control(x, dev, 0x80, 6, 0x0200, 0, total))
        return "Konfiguration nicht lesbar";
    if (parse_configuration(dev, total, dev->speed) == 0)
        return "weder Tastatur noch Maus";

    if (!control(x, dev, 0x00, 9, config_value, 0, 0))     /* SET_CONFIGURATION */
        return "Konfiguration nicht wählbar";

    /* Endpunkte für die Berichte beim Controller anmelden */
    uint32_t add_flags = 1;
    uint32_t last_dci = 1;
    for (int i = 0; i < 2; i++) {
        hid_t *hid = &dev->hid[i];
        if (hid->kind == HID_NONE)
            continue;
        ring_init(&hid->ring);
        hid->buffer = page_get();

        uint32_t *ep = context(x, dev->in_ctx, hid->dci + 1);
        ep[0] = (uint32_t)hid->interval << 16;
        ep[1] = (3u << 1) | (7u << 3) | ((uint32_t)hid->max_packet << 16);
        ep[2] = (uint32_t)(uint64_t)hid->ring.trbs | 1;
        ep[3] = (uint32_t)((uint64_t)hid->ring.trbs >> 32);
        ep[4] = hid->max_packet | ((uint32_t)hid->max_packet << 16);
        add_flags |= 1u << hid->dci;
        if (hid->dci > last_dci)
            last_dci = hid->dci;
    }
    ctrl[0] = 0;
    ctrl[1] = add_flags;
    slot[0] = ((uint32_t)dev->speed << 20) | (last_dci << 27);
    if (!command(x, (uint64_t)dev->in_ctx,
                 TRB_TYPE(TRB_CONFIGURE_ENDPOINT) |
                     ((uint32_t)dev->slot << 24)))
        return "Endpunkt nicht einrichtbar";

    for (int i = 0; i < 2; i++) {
        hid_t *hid = &dev->hid[i];
        if (hid->kind == HID_NONE)
            continue;
        /* Einfaches Boot-Protokoll wählen; Tastatur: nur Änderungen melden */
        control(x, dev, 0x21, 0x0B, 0, hid->interface, 0);
        if (hid->kind == HID_KEYBOARD)
            control(x, dev, 0x21, 0x0A, 0, hid->interface, 0);
        hid_request(x, dev, hid);
        klog("USB %d: %s an Anschluss %d\n", x->number,
             hid->kind == HID_KEYBOARD ? "Tastatur" : "Maus", dev->port + 1);
    }
    return NULL;
}

static void port_connected(xhci_t *x, int port)
{
    if (x->failures[port] >= 3)
        return;                         /* aufgegeben, bis neu eingesteckt wird */

    device_t *dev = NULL;
    for (int i = 0; i < MAX_DEVICES && !dev; i++) {
        if (!x->devices[i].used)
            dev = &x->devices[i];
    }
    if (!dev)
        return;

    task_sleep(100);                    /* Kontakte zur Ruhe kommen lassen */
    if (!port_reset(x, port)) {
        if (++x->failures[port] == 3)
            klog("USB %d: Anschluss %d lässt sich nicht aktivieren\n",
                 x->number, port + 1);
        return;
    }
    memset(dev, 0, sizeof(*dev));
    dev->used = true;
    dev->port = (uint8_t)port;
    dev->speed = (uint8_t)PORT_SPEED(rd32(x->op, OP_PORTSC(port)));

    const char *problem = device_setup(x, dev);
    if (problem) {
        /* Kein Eingabegerät oder Fehler: Anschluss in Ruhe lassen */
        klog("USB %d: Gerät an Anschluss %d ignoriert (%s)\n", x->number,
             port + 1, problem);
        device_free(x, dev);
        x->failures[port] = 3;
    }
}

static void scan_ports(xhci_t *x)
{
    for (int port = 0; port < x->ports; port++) {
        uint32_t status = rd32(x->op, OP_PORTSC(port));
        device_t *dev = NULL;
        for (int i = 0; i < MAX_DEVICES; i++) {
            if (x->devices[i].used && x->devices[i].port == port)
                dev = &x->devices[i];
        }

        if (status & PORT_CONNECT_CHANGE) {
            wr32(x->op, OP_PORTSC(port),
                 (status & PORT_KEEP_BITS) | PORT_CONNECT_CHANGE);
            x->failures[port] = 0;
            if (dev) {                  /* abgesteckt (oder neu eingesteckt) */
                klog("USB %d: Gerät an Anschluss %d entfernt\n", x->number,
                     port + 1);
                device_free(x, dev);
                dev = NULL;
            }
        }
        if ((status & PORT_CONNECTED) && !dev)
            port_connected(x, port);
    }
}

/* --- Controller starten ---------------------------------------------------------------- */

/* Die Firmware bedient USB-Geräte evtl. noch selbst: Kontrolle übernehmen. */
static void take_ownership(xhci_t *x)
{
    uint32_t offset = (rd32(x->cap, CAP_HCCPARAMS1) >> 16) << 2;
    for (int guard = 0; offset && guard < 64; guard++) {
        uint32_t value = rd32(x->cap, offset);
        if ((value & 0xFF) == 1) {      /* "USB Legacy Support" */
            if (value & (1u << 16)) {
                wr32(x->cap, offset, value | (1u << 24));
                wait_bits(x->cap, offset, 1u << 16, 0, 1000);
            }
            /* Systemverwaltungs-Interrupts der Firmware abschalten */
            uint32_t legacy = rd32(x->cap, offset + 4);
            legacy &= (0x7u << 1) | (0xFFu << 5) | (0x7u << 17);
            legacy |= 0x7u << 29;
            wr32(x->cap, offset + 4, legacy);
        }
        uint32_t next = (value >> 8) & 0xFF;
        if (!next)
            break;
        offset += next << 2;
    }
}

static bool controller_start(xhci_t *x)
{
    const pci_device_t *pci = &x->pci;

    uint64_t bar = pci_read32(pci, 0x10);
    if (bar & 1)
        return false;                   /* kein Speicherbereich */
    if ((bar & 0x6) == 0x4)
        bar |= (uint64_t)pci_read32(pci, 0x14) << 32;
    bar &= ~0xFull;
    if (!bar)
        return false;

    /* Gerät einschalten: Speicherzugriff + Busmaster, kein Interrupt-Pin */
    uint16_t pci_command = pci_read16(pci, 0x04);
    pci_write16(pci, 0x04, pci_command | 0x0006 | 0x0400);

    /* Ältere Intel-Chipsätze: Anschlüsse vom USB-2-Controller herüberholen */
    if (pci->vendor_id == 0x8086) {
        pci_write32(pci, 0xD8, pci_read32(pci, 0xDC));
        pci_write32(pci, 0xD0, pci_read32(pci, 0xD4));
    }

    vmm_map(bar, 0x20000);
    x->cap = (uint8_t *)bar;
    x->op = x->cap + (rd32(x->cap, CAP_CAPLENGTH) & 0xFF);
    x->runtime = x->cap + (rd32(x->cap, CAP_RTSOFF) & ~0x1Fu);
    x->doorbell = (volatile uint32_t *)(x->cap + (rd32(x->cap, CAP_DBOFF) & ~0x3u));

    uint32_t params1 = rd32(x->cap, CAP_HCSPARAMS1);
    uint32_t params2 = rd32(x->cap, CAP_HCSPARAMS2);
    uint32_t cparams = rd32(x->cap, CAP_HCCPARAMS1);
    uint32_t slots = params1 & 0xFF;
    x->ports = (int)(params1 >> 24);
    if (x->ports > MAX_PORTS)
        x->ports = MAX_PORTS;
    x->context_size = (cparams & (1u << 2)) ? 64 : 32;

    take_ownership(x);

    /* Anhalten und zurücksetzen */
    wr32(x->op, OP_USBCMD, rd32(x->op, OP_USBCMD) & ~CMD_RUN);
    wait_bits(x->op, OP_USBSTS, STS_HALTED, STS_HALTED, 100);
    wr32(x->op, OP_USBCMD, CMD_RESET);
    task_sleep(2);                      /* manche Controller reagieren träge */
    if (!wait_bits(x->op, OP_USBCMD, CMD_RESET, 0, 1000) ||
        !wait_bits(x->op, OP_USBSTS, STS_NOT_READY, 0, 1000))
        return false;

    wr32(x->op, OP_CONFIG, slots);

    /* Tabelle der Gerätekontexte; Eintrag 0 gehört dem Controller selbst */
    x->dcbaa = page_get();
    uint32_t scratch = ((params2 >> 27) & 0x1F) | (((params2 >> 21) & 0x1F) << 5);
    if (scratch) {
        uint32_t page_units = rd32(x->op, OP_PAGESIZE) & 0xFFFF;
        size_t pages = page_units ? page_units : 1;
        uint64_t *table = alloc_pages((scratch * 8 + PAGE_SIZE - 1) / PAGE_SIZE);
        for (uint32_t i = 0; i < scratch; i++)
            table[i] = (uint64_t)alloc_pages(pages);
        x->dcbaa[0] = (uint64_t)table;
    }
    wr64(x->op, OP_DCBAAP, (uint64_t)x->dcbaa);

    /* Kommandoring */
    ring_init(&x->commands);
    wr64(x->op, OP_CRCR, (uint64_t)x->commands.trbs | 1);

    /* Ereignisring: Hierüber meldet der Controller alles zurück. */
    x->events = page_get();
    x->event_dequeue = 0;
    x->event_cycle = 1;
    uint64_t *segments = page_get();
    segments[0] = (uint64_t)x->events;
    segments[1] = RING_TRBS;
    wr32(x->runtime, IR_ERSTSZ, 1);
    wr64(x->runtime, IR_ERDP, (uint64_t)x->events);
    wr64(x->runtime, IR_ERSTBA, (uint64_t)segments);

    wr32(x->op, OP_USBCMD, CMD_RUN);
    if (!wait_bits(x->op, OP_USBSTS, STS_HALTED, 0, 1000))
        return false;

    /* Anschlüsse mit Strom versorgen */
    for (int port = 0; port < x->ports; port++) {
        uint32_t status = rd32(x->op, OP_PORTSC(port));
        if (!(status & PORT_POWER))
            wr32(x->op, OP_PORTSC(port), (status & PORT_KEEP_BITS) | PORT_POWER);
    }
    return true;
}

static void xhci_task(void *arg)
{
    xhci_t *x = arg;

    if (!controller_start(x)) {
        klog("USB %d: Controller lässt sich nicht starten\n", x->number);
        return;
    }
    klog("USB %d: xHCI-Controller mit %d Anschlüssen gestartet\n", x->number,
         x->ports);
    task_sleep(200);

    uint64_t next_scan = 0;
    for (;;) {
        poll_events(x);

        uint64_t now = time_ms();
        if (now >= next_scan) {
            scan_ports(x);
            next_scan = time_ms() + 250;
        }
        if (x->repeat_key && now >= x->repeat_at) {
            send_key(x->repeat_key, false);
            x->repeat_at = now + 33;
        }
        task_sleep(2);
    }
}

void usb_init(void)
{
    pci_device_t found[MAX_CONTROLLERS];
    int count = pci_find(0x0C, 0x03, 0x30, found, MAX_CONTROLLERS);
    if (count == 0) {
        klog("USB: kein xHCI-Controller gefunden\n");
        return;
    }
    for (int i = 0; i < count; i++) {
        xhci_t *x = kzalloc(sizeof(*x));
        if (!x)
            return;
        x->pci = found[i];
        x->number = i + 1;
        task_create("usb", xhci_task, x, serial_console());
    }
}
