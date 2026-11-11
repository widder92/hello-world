/*
 * Netzwerk-Stack, Teil 1: alles unterhalb von TCP.
 *
 * Ein eigener Task ("netz") holt empfangene Pakete von der Netzwerkkarte ab
 * und reicht sie durch die Schichten: Ethernet -> ARP oder IPv4 -> ICMP, UDP
 * oder TCP. Derselbe Task kümmert sich um alles Zeitgesteuerte (DHCP,
 * erneutes Senden bei TCP).
 *
 * Programme rufen die Funktionen aus net.h auf und warten dort, bis der
 * Netzwerk-Task sie weckt. Gemeinsame Daten sind wie überall im Kernel durch
 * kurzes Sperren der Interrupts geschützt.
 */
#include "net.h"
#include "pci.h"
#include "console.h"
#include "cpu.h"
#include "crypto.h"
#include "string.h"
#include "task.h"
#include "time.h"

#define ETH_HEADER      14
#define ETH_TYPE_IP     0x0800
#define ETH_TYPE_ARP    0x0806
#define IP_HEADER       20
#define IP_BROADCAST    0xFFFFFFFFu

static nic_t *nic;
static bool link_up;
static bool configured;
static ip4_t local_ip, netmask, gateway, dns_server;
static uint64_t rx_packets, tx_packets;

static const uint8_t mac_broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/* --- Hilfsfunktionen -------------------------------------------------------------- */

bool ip4_parse(const char *text, ip4_t *out)
{
    uint32_t value = 0;
    for (int part = 0; part < 4; part++) {
        uint32_t number = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9') {
            number = number * 10 + (uint32_t)(*text++ - '0');
            if (++digits > 3 || number > 255)
                return false;
        }
        if (digits == 0)
            return false;
        value = (value << 8) | number;
        if (part < 3 && *text++ != '.')
            return false;
    }
    if (*text)
        return false;
    *out = value;
    return true;
}

void ip4_format(ip4_t ip, char out[16])
{
    ksnprintf(out, 16, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 0xFF,
              (ip >> 8) & 0xFF, ip & 0xFF);
}

/* Internet-Prüfsumme: Summe aller 16-Bit-Worte, Überträge zurückgefaltet. */
uint16_t net_checksum(const void *data, size_t len, uint32_t initial)
{
    const uint8_t *p = data;
    uint32_t sum = initial;
    for (; len >= 2; len -= 2, p += 2)
        sum += (uint32_t)((p[0] << 8) | p[1]);
    if (len)
        sum += (uint32_t)(p[0] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

ip4_t net_local_ip(void)
{
    return local_ip;
}

/*
 * Legt den laufenden Task schlafen, bis ihn jemand über *waiter weckt oder
 * der Zeitpunkt erreicht ist. Aufruf mit gesperrten Interrupts.
 */
void net_wait(uint32_t *waiter, uint64_t deadline_ms)
{
    uint64_t now = time_ms();
    if (now >= deadline_ms)
        return;
    *waiter = task_current_pid();
    task_block((uint32_t)(deadline_ms - now));
    *waiter = 0;
}

static void wake(uint32_t *waiter)
{
    if (*waiter)
        task_wake(*waiter);
}

/* --- Ethernet und ARP -------------------------------------------------------------- */

static void eth_send(const uint8_t *dst_mac, uint16_t type, uint8_t *frame,
                     size_t payload_len)
{
    memcpy(frame, dst_mac, 6);
    memcpy(frame + 6, nic->mac, 6);
    put_be16(frame + 12, type);
    if (nic->send(nic, frame, ETH_HEADER + payload_len))
        tx_packets++;
}

/* ARP übersetzt IP-Adressen im eigenen Netz in Hardware-Adressen (MAC). */
#define ARP_ENTRIES 8

static struct arp_entry {
    ip4_t ip;                           /* 0 = frei */
    uint8_t mac[6];
    bool resolved;
    uint64_t used_ms;
    uint64_t asked_ms;
    /* Ein Paket, das auf die Antwort wartet */
    uint8_t pending[ETH_FRAME_MAX];
    size_t pending_len;
} arp_table[ARP_ENTRIES];

static void arp_send(uint16_t operation, const uint8_t *target_mac,
                     ip4_t target_ip, const uint8_t *eth_dst)
{
    uint8_t frame[ETH_HEADER + 28];
    uint8_t *p = frame + ETH_HEADER;
    put_be16(p, 1);                     /* Hardware: Ethernet */
    put_be16(p + 2, ETH_TYPE_IP);
    p[4] = 6;
    p[5] = 4;
    put_be16(p + 6, operation);
    memcpy(p + 8, nic->mac, 6);
    put_be32(p + 14, local_ip);
    memcpy(p + 18, target_mac, 6);
    put_be32(p + 24, target_ip);
    eth_send(eth_dst, ETH_TYPE_ARP, frame, 28);
}

static struct arp_entry *arp_find(ip4_t ip, bool create)
{
    struct arp_entry *oldest = &arp_table[0];
    for (int i = 0; i < ARP_ENTRIES; i++) {
        if (arp_table[i].ip == ip)
            return &arp_table[i];
        if (arp_table[i].used_ms < oldest->used_ms)
            oldest = &arp_table[i];
    }
    if (!create)
        return NULL;
    memset(oldest, 0, sizeof(*oldest)); /* ältesten Eintrag verdrängen */
    oldest->ip = ip;
    return oldest;
}

static void arp_input(const uint8_t *p, size_t len)
{
    if (len < 28 || get_be16(p) != 1 || get_be16(p + 2) != ETH_TYPE_IP)
        return;
    uint16_t operation = get_be16(p + 6);
    const uint8_t *sender_mac = p + 8;
    ip4_t sender_ip = get_be32(p + 14);
    ip4_t target_ip = get_be32(p + 24);

    /* Antworten (und Anfragen an uns) verraten die Adresse des Absenders. */
    struct arp_entry *entry = arp_find(sender_ip, false);
    if (entry) {
        memcpy(entry->mac, sender_mac, 6);
        entry->resolved = true;
        if (entry->pending_len) {       /* wartendes Paket jetzt losschicken */
            memcpy(entry->pending, entry->mac, 6);
            if (nic->send(nic, entry->pending, entry->pending_len))
                tx_packets++;
            entry->pending_len = 0;
        }
    }
    if (operation == 1 && local_ip && target_ip == local_ip)
        arp_send(2, sender_mac, sender_ip, sender_mac);
}

/* --- IPv4 --------------------------------------------------------------------------- */

static uint16_t ip_ident;

bool ip_send(ip4_t dst, uint8_t protocol, const void *payload, size_t len)
{
    if (!nic || len > ETH_FRAME_MAX - ETH_HEADER - IP_HEADER)
        return false;

    uint8_t frame[ETH_FRAME_MAX];
    uint8_t *ip = frame + ETH_HEADER;
    uint64_t flags = irq_save();

    ip[0] = 0x45;                       /* Version 4, Kopf 20 Bytes */
    ip[1] = 0;
    put_be16(ip + 2, (uint16_t)(IP_HEADER + len));
    put_be16(ip + 4, ip_ident++);
    put_be16(ip + 6, 0x4000);           /* nicht zerteilen */
    ip[8] = 64;                         /* Lebensdauer (TTL) */
    ip[9] = protocol;
    put_be16(ip + 10, 0);
    put_be32(ip + 12, local_ip);
    put_be32(ip + 16, dst);
    put_be16(ip + 10, net_checksum(ip, IP_HEADER, 0));
    memcpy(ip + IP_HEADER, payload, len);
    size_t frame_len = ETH_HEADER + IP_HEADER + len;

    if (dst == IP_BROADCAST) {
        eth_send(mac_broadcast, ETH_TYPE_IP, frame, IP_HEADER + len);
        irq_restore(flags);
        return true;
    }

    /* Ziele außerhalb des eigenen Netzes gehen an den Router. */
    ip4_t next_hop = dst;
    if (gateway && ((dst ^ local_ip) & netmask))
        next_hop = gateway;

    struct arp_entry *entry = arp_find(next_hop, true);
    entry->used_ms = time_ms();
    if (entry->resolved) {
        eth_send(entry->mac, ETH_TYPE_IP, frame, IP_HEADER + len);
    } else {
        /* Adresse noch unbekannt: Paket parken und nachfragen */
        memcpy(frame + 6, nic->mac, 6);
        put_be16(frame + 12, ETH_TYPE_IP);
        memcpy(entry->pending, frame, frame_len);
        entry->pending_len = frame_len;
        if (!entry->asked_ms || time_ms() - entry->asked_ms > 500) {
            static const uint8_t zero_mac[6];
            entry->asked_ms = time_ms();
            arp_send(1, zero_mac, next_hop, mac_broadcast);
        }
    }
    irq_restore(flags);
    return true;
}

/* --- ICMP (ping) --------------------------------------------------------------------- */

static struct {
    uint16_t sequence;
    bool answered;
    uint32_t waiter;
} ping;

static void icmp_input(ip4_t src, const uint8_t *p, size_t len)
{
    if (len < 8 || net_checksum(p, len, 0) != 0)
        return;
    if (p[0] == 8) {                    /* Echo-Anfrage: unverändert zurück */
        uint8_t reply[ETH_FRAME_MAX];
        if (len > sizeof(reply) - ETH_HEADER - IP_HEADER)
            return;
        memcpy(reply, p, len);
        reply[0] = 0;
        put_be16(reply + 2, 0);
        put_be16(reply + 2, net_checksum(reply, len, 0));
        ip_send(src, IP_PROTO_ICMP, reply, len);
    } else if (p[0] == 0 && get_be16(p + 6) == ping.sequence) {
        ping.answered = true;
        wake(&ping.waiter);
    }
}

bool net_ping(ip4_t target, uint32_t timeout_ms, uint32_t *rtt_ms)
{
    uint8_t packet[8 + 32];
    uint64_t flags = irq_save();
    ping.sequence++;
    ping.answered = false;
    packet[0] = 8;                      /* Echo-Anfrage */
    packet[1] = 0;
    put_be16(packet + 2, 0);
    put_be16(packet + 4, 0x4F53);       /* Kennung */
    put_be16(packet + 6, ping.sequence);
    for (int i = 8; i < (int)sizeof(packet); i++)
        packet[i] = (uint8_t)('a' + i % 26);
    put_be16(packet + 2, net_checksum(packet, sizeof(packet), 0));

    uint64_t start = time_us();
    uint64_t deadline = time_ms() + timeout_ms;
    ip_send(target, IP_PROTO_ICMP, packet, sizeof(packet));
    while (!ping.answered && time_ms() < deadline)
        net_wait(&ping.waiter, deadline);
    bool ok = ping.answered;
    if (rtt_ms)
        *rtt_ms = (uint32_t)((time_us() - start + 500) / 1000);
    irq_restore(flags);
    return ok;
}

/* --- UDP ------------------------------------------------------------------------------ */

static void udp_send(ip4_t dst, uint16_t src_port, uint16_t dst_port,
                     const void *data, size_t len)
{
    uint8_t packet[1472 + 8];
    if (len > sizeof(packet) - 8)
        return;
    put_be16(packet, src_port);
    put_be16(packet + 2, dst_port);
    put_be16(packet + 4, (uint16_t)(8 + len));
    put_be16(packet + 6, 0);            /* Prüfsumme ist bei IPv4 freiwillig */
    memcpy(packet + 8, data, len);
    ip_send(dst, IP_PROTO_UDP, packet, 8 + len);
}

/* --- DHCP: Adresse automatisch beziehen ---------------------------------------------- */

enum { DHCP_DISCOVER = 1, DHCP_OFFER = 2, DHCP_REQUEST = 3, DHCP_ACK = 5, DHCP_NAK = 6 };

static struct {
    enum { DHCP_IDLE, DHCP_SELECTING, DHCP_REQUESTING, DHCP_BOUND } state;
    uint32_t xid;
    ip4_t offered, server;
    uint64_t next_ms;                   /* nächster Sendeversuch / Erneuerung */
    int attempts;
} dhcp;

static void dhcp_send(uint8_t type)
{
    uint8_t msg[300];
    memset(msg, 0, sizeof(msg));
    msg[0] = 1;                         /* Anfrage */
    msg[1] = 1;                         /* Ethernet */
    msg[2] = 6;
    put_be32(msg + 4, dhcp.xid);
    put_be16(msg + 10, 0x8000);         /* Antwort bitte als Rundruf */
    memcpy(msg + 28, nic->mac, 6);
    put_be32(msg + 236, 0x63825363);    /* Kennung für DHCP-Optionen */

    uint8_t *opt = msg + 240;
    *opt++ = 53;                        /* Nachrichtentyp */
    *opt++ = 1;
    *opt++ = type;
    if (type == DHCP_REQUEST) {
        *opt++ = 50;                    /* gewünschte Adresse */
        *opt++ = 4;
        put_be32(opt, dhcp.offered);
        opt += 4;
        if (dhcp.server) {
            *opt++ = 54;                /* welcher Server gemeint ist */
            *opt++ = 4;
            put_be32(opt, dhcp.server);
            opt += 4;
        }
    }
    *opt++ = 55;                        /* gewünschte Angaben */
    *opt++ = 3;
    *opt++ = 1;                         /* Netzmaske */
    *opt++ = 3;                         /* Router */
    *opt++ = 6;                         /* DNS-Server */
    *opt++ = 255;

    /* Solange wir keine Adresse haben, ist der Absender 0.0.0.0. */
    ip4_t saved = local_ip;
    if (dhcp.state != DHCP_BOUND)
        local_ip = 0;
    udp_send(IP_BROADCAST, 68, 67, msg, sizeof(msg));
    local_ip = saved;
}

static void dhcp_input(const uint8_t *msg, size_t len)
{
    if (len < 240 || msg[0] != 2 || get_be32(msg + 4) != dhcp.xid ||
        get_be32(msg + 236) != 0x63825363)
        return;

    uint8_t type = 0;
    ip4_t mask = 0, router = 0, dns = 0, server = 0;
    uint32_t lease = 3600;
    for (size_t i = 240; i + 1 < len && msg[i] != 255; ) {
        if (msg[i] == 0) {              /* Füllbyte */
            i++;
            continue;
        }
        uint8_t code = msg[i], size = msg[i + 1];
        const uint8_t *value = msg + i + 2;
        if (i + 2 + size > len)
            break;
        if (code == 53 && size >= 1)
            type = value[0];
        else if (code == 1 && size >= 4)
            mask = get_be32(value);
        else if (code == 3 && size >= 4)
            router = get_be32(value);
        else if (code == 6 && size >= 4)
            dns = get_be32(value);
        else if (code == 51 && size >= 4)
            lease = get_be32(value);
        else if (code == 54 && size >= 4)
            server = get_be32(value);
        i += 2 + size;
    }

    if (type == DHCP_OFFER && dhcp.state == DHCP_SELECTING) {
        dhcp.offered = get_be32(msg + 16);
        dhcp.server = server;
        dhcp.state = DHCP_REQUESTING;
        dhcp.attempts = 0;
        dhcp_send(DHCP_REQUEST);
        dhcp.next_ms = time_ms() + 2000;
    } else if (type == DHCP_ACK && dhcp.state == DHCP_REQUESTING) {
        bool first = !configured;
        local_ip = get_be32(msg + 16);
        netmask = mask ? mask : IP4(255, 255, 255, 0);
        gateway = router;
        if (!dns_server || first)
            dns_server = dns;
        configured = true;
        random_stir(msg, len);
        dhcp.state = DHCP_BOUND;
        dhcp.next_ms = time_ms() + (uint64_t)(lease < 60 ? 60 : lease) * 500;
        if (first) {
            char text[16];
            ip4_format(local_ip, text);
            klog("Netzwerk: Adresse %s per DHCP erhalten\n", text);
        }
    } else if (type == DHCP_NAK) {
        dhcp.state = DHCP_IDLE;
        dhcp.next_ms = time_ms() + 1000;
    }
}

static void dhcp_timer(void)
{
    uint64_t now = time_ms();
    if (now < dhcp.next_ms)
        return;

    switch (dhcp.state) {
    case DHCP_IDLE:
    case DHCP_SELECTING:
        dhcp.state = DHCP_SELECTING;
        dhcp.xid = (uint32_t)rdtsc() ^ get_be32(nic->mac + 2);
        dhcp.server = 0;
        dhcp_send(DHCP_DISCOVER);
        dhcp.next_ms = now + (dhcp.attempts < 3 ? 2000 : 8000);
        dhcp.attempts++;
        break;
    case DHCP_REQUESTING:
        if (++dhcp.attempts > 3) {      /* keine Bestätigung: von vorn */
            dhcp.state = DHCP_IDLE;
            dhcp.attempts = 0;
        } else {
            dhcp_send(DHCP_REQUEST);
            dhcp.next_ms = now + 2000;
        }
        break;
    case DHCP_BOUND:                    /* halbe Laufzeit um: verlängern */
        dhcp.offered = local_ip;
        dhcp.server = 0;
        dhcp.state = DHCP_REQUESTING;
        dhcp.attempts = 0;
        dhcp_send(DHCP_REQUEST);
        dhcp.next_ms = now + 2000;
        break;
    }
}

/* --- DNS: Namen in Adressen übersetzen ------------------------------------------------ */

static struct {
    bool busy;
    uint16_t id, port;
    bool done;
    ip4_t result;
    uint32_t waiter;
} dns;

/* Überspringt einen (evtl. komprimierten) Namen in einer DNS-Nachricht. */
static size_t dns_skip_name(const uint8_t *msg, size_t len, size_t pos)
{
    while (pos < len) {
        uint8_t label = msg[pos];
        if (label == 0)
            return pos + 1;
        if ((label & 0xC0) == 0xC0)     /* Verweis auf eine frühere Stelle */
            return pos + 2;
        pos += 1 + label;
    }
    return len;
}

static void dns_input(const uint8_t *msg, size_t len)
{
    if (!dns.busy || len < 12 || get_be16(msg) != dns.id || !(msg[2] & 0x80))
        return;
    uint16_t questions = get_be16(msg + 4);
    uint16_t answers = get_be16(msg + 6);

    size_t pos = 12;
    for (uint16_t i = 0; i < questions; i++)
        pos = dns_skip_name(msg, len, pos) + 4;
    for (uint16_t i = 0; i < answers && pos < len; i++) {
        pos = dns_skip_name(msg, len, pos);
        if (pos + 10 > len)
            break;
        uint16_t type = get_be16(msg + pos);
        uint16_t size = get_be16(msg + pos + 8);
        pos += 10;
        if (pos + size > len)
            break;
        if (type == 1 && size == 4 && !dns.result)  /* A-Eintrag: IPv4-Adresse */
            dns.result = get_be32(msg + pos);
        pos += size;
    }
    dns.done = true;
    wake(&dns.waiter);
}

bool net_resolve(const char *name, ip4_t *out)
{
    if (ip4_parse(name, out))
        return true;
    if (!configured || !dns_server)
        return false;

    /* Anfrage bauen: Kopf, dann der Name als Folge von Abschnitten */
    uint8_t query[12 + 256 + 4];
    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > 253)
        return false;
    memset(query, 0, 12);
    query[2] = 0x01;                    /* bitte rekursiv auflösen */
    query[5] = 1;                       /* eine Frage */
    size_t pos = 12;
    while (*name) {
        size_t label = 0;
        while (name[label] && name[label] != '.')
            label++;
        if (label == 0 || label > 63)
            return false;
        query[pos++] = (uint8_t)label;
        memcpy(query + pos, name, label);
        pos += label;
        name += label + (name[label] == '.');
    }
    query[pos++] = 0;
    put_be16(query + pos, 1);           /* Typ A */
    put_be16(query + pos + 2, 1);       /* Klasse Internet */
    pos += 4;

    uint64_t flags = irq_save();
    while (dns.busy) {                  /* immer nur eine Anfrage zur Zeit */
        irq_restore(flags);
        task_sleep(10);
        flags = irq_save();
    }
    dns.busy = true;
    dns.result = 0;

    for (int attempt = 0; attempt < 3 && !dns.result; attempt++) {
        dns.id = (uint16_t)rdtsc();
        dns.port = (uint16_t)(49152 + (rdtsc() >> 8) % 16000);
        dns.done = false;
        put_be16(query, dns.id);
        udp_send(dns_server, dns.port, 53, query, pos);

        uint64_t deadline = time_ms() + 2000;
        while (!dns.done && time_ms() < deadline)
            net_wait(&dns.waiter, deadline);
        if (dns.done && !dns.result)
            break;                      /* Antwort da, aber ohne Adresse */
    }
    *out = dns.result;
    bool ok = dns.result != 0;
    dns.busy = false;
    irq_restore(flags);
    return ok;
}

/* --- Empfang ---------------------------------------------------------------------------- */

static void udp_input(const uint8_t *p, size_t len)
{
    if (len < 8)
        return;
    uint16_t dst_port = get_be16(p + 2);
    size_t udp_len = get_be16(p + 4);
    if (udp_len < 8 || udp_len > len)
        return;
    if (dst_port == 68)
        dhcp_input(p + 8, udp_len - 8);
    else if (dns.busy && dst_port == dns.port)
        dns_input(p + 8, udp_len - 8);
}

static void ip_input(const uint8_t *ip, size_t len)
{
    if (len < IP_HEADER || (ip[0] >> 4) != 4)
        return;
    size_t header = (size_t)(ip[0] & 0x0F) * 4;
    size_t total = get_be16(ip + 2);
    if (header < IP_HEADER || total < header || total > len)
        return;
    if (net_checksum(ip, header, 0) != 0)
        return;
    if (get_be16(ip + 6) & 0x3FFF)
        return;                         /* zerteilte Pakete: nicht unterstützt */

    ip4_t src = get_be32(ip + 12);
    ip4_t dst = get_be32(ip + 16);
    bool for_us = dst == IP_BROADCAST || (local_ip && dst == local_ip) ||
                  (local_ip && dst == (local_ip | ~netmask));
    if (!for_us && configured)
        return;

    const uint8_t *payload = ip + header;
    size_t payload_len = total - header;
    switch (ip[9]) {
    case IP_PROTO_ICMP:
        if (configured)
            icmp_input(src, payload, payload_len);
        break;
    case IP_PROTO_UDP:
        udp_input(payload, payload_len);
        break;
    case IP_PROTO_TCP:
        if (configured && dst == local_ip)
            tcp_input(src, payload, payload_len);
        break;
    }
}

static void frame_input(const uint8_t *frame, size_t len)
{
    if (len < ETH_HEADER)
        return;
    rx_packets++;
    uint16_t type = get_be16(frame + 12);
    if (type == ETH_TYPE_ARP)
        arp_input(frame + ETH_HEADER, len - ETH_HEADER);
    else if (type == ETH_TYPE_IP)
        ip_input(frame + ETH_HEADER, len - ETH_HEADER);
}

/* --- Der Netzwerk-Task -------------------------------------------------------------------- */

static void net_task(void *arg)
{
    (void)arg;
    static uint8_t frame[2048];
    uint64_t link_checked = 0;

    for (;;) {
        uint64_t flags = irq_save();
        for (int i = 0; i < 64; i++) {
            size_t len = nic->receive(nic, frame, sizeof(frame));
            if (!len)
                break;
            frame_input(frame, len);
        }

        uint64_t now = time_ms();
        if (now - link_checked >= 500) {
            link_checked = now;
            bool up = nic->link_up(nic);
            if (up != link_up) {
                link_up = up;
                klog("Netzwerk: Verbindung %s\n", up ? "hergestellt" : "getrennt");
                if (!up) {              /* Kabel gezogen: Adresse verfällt */
                    configured = false;
                    local_ip = 0;
                    dhcp.state = DHCP_IDLE;
                    dhcp.attempts = 0;
                }
                dhcp.next_ms = now;
            }
        }
        if (link_up)
            dhcp_timer();
        tcp_timer();
        irq_restore(flags);

        task_sleep(1);
    }
}

void net_init(void)
{
    nic = e1000_probe();
    if (!nic)
        nic = igc_probe();
    if (!nic) {
        /* Sagen, was da ist - das hilft, wenn eine Karte (noch) nicht unterstützt wird. */
        pci_device_t found[4];
        int count = pci_find(0x02, 0x00, 0x00, found, 4);
        for (int i = 0; i < count; i++)
            klog("Netzwerk: Karte %04x:%04x wird nicht unterstützt\n", found[i].vendor_id,
                 found[i].device_id);
        count = pci_find(0x02, 0x80, 0x00, found, 4);
        for (int i = 0; i < count; i++)
            klog("Netzwerk: WLAN-Karte %04x:%04x wird nicht unterstützt\n",
                 found[i].vendor_id, found[i].device_id);
    }
    if (!nic) {
        klog("Netzwerk: keine unterstützte Netzwerkkarte gefunden\n");
        return;
    }
    klog("Netzwerk: %s, MAC %02x:%02x:%02x:%02x:%02x:%02x\n", nic->name,
         nic->mac[0], nic->mac[1], nic->mac[2], nic->mac[3], nic->mac[4],
         nic->mac[5]);
    task_create("netz", net_task, NULL, NULL);
}

void net_get_info(net_info_t *out)
{
    uint64_t flags = irq_save();
    memset(out, 0, sizeof(*out));
    if (nic) {
        out->present = true;
        out->driver = nic->name;
        memcpy(out->mac, nic->mac, 6);
        out->link = link_up;
        out->configured = configured;
        out->ip = local_ip;
        out->netmask = netmask;
        out->gateway = gateway;
        out->dns = dns_server;
        out->rx_packets = rx_packets;
        out->tx_packets = tx_packets;
    }
    irq_restore(flags);
}

void net_set_dns(ip4_t server)
{
    dns_server = server;
}

bool net_wait_configured(uint32_t timeout_ms)
{
    uint64_t deadline = time_ms() + timeout_ms;
    while (nic && !configured && time_ms() < deadline)
        task_sleep(50);
    return configured;
}
