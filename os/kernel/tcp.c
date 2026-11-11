/*
 * Netzwerk-Stack, Teil 2: TCP - zuverlässige Verbindungen.
 *
 * Bewusst einfach gehalten: nur ausgehende Verbindungen, Daten werden streng
 * der Reihe nach angenommen (was zu früh kommt, wird verworfen und von der
 * Gegenseite erneut gesendet), und bei ausbleibender Bestätigung wird alles
 * Unbestätigte wiederholt.
 */
#include "cpu.h"
#include "memory.h"
#include "net.h"
#include "string.h"
#include "task.h"
#include "time.h"

#define MAX_CONNECTIONS 8
#define TCP_HEADER      20
#define MSS_DEFAULT     536
#define MSS_OURS        1460
#define RX_BUFFER       65535
#define TX_BUFFER       16384
#define FLIGHT_MAX      (4 * MSS_OURS)  /* höchstens so viel unbestätigt */
#define RTO_INITIAL     1000
#define RETRIES_MAX     6

#define FLAG_FIN 0x01
#define FLAG_SYN 0x02
#define FLAG_RST 0x04
#define FLAG_PSH 0x08
#define FLAG_ACK 0x10

typedef enum { TCP_FREE, TCP_SYN_SENT, TCP_OPEN, TCP_DEAD } tcp_state_t;

struct tcp_conn {
    tcp_state_t state;
    ip4_t remote;
    uint16_t local_port, remote_port;

    uint32_t snd_una;                   /* älteste unbestätigte Sequenznummer */
    uint32_t rcv_nxt;                   /* nächste erwartete Sequenznummer */
    uint32_t snd_wnd;                   /* Empfangsfenster der Gegenseite */
    uint32_t mss;

    /* Sendepuffer: tx[0] entspricht snd_una; tx_sent Bytes sind unterwegs */
    uint8_t *tx;
    uint32_t tx_len, tx_sent;
    bool fin_queued, fin_sent, fin_acked;

    /* Empfangspuffer (Ring) */
    uint8_t *rx;
    uint32_t rx_head, rx_count;
    bool peer_fin;

    uint64_t rto_at;                    /* 0 = Wecker aus */
    uint32_t rto_ms;
    int retries;

    uint32_t waiter;
};

static tcp_conn_t connections[MAX_CONNECTIONS];
static uint16_t next_port;

/* --- Senden ------------------------------------------------------------------------ */

static void send_segment(tcp_conn_t *c, uint32_t seq, uint8_t flags,
                         const uint8_t *data, uint32_t len)
{
    uint8_t segment[TCP_HEADER + 4 + MSS_OURS];
    uint32_t header = TCP_HEADER;

    put_be16(segment, c->local_port);
    put_be16(segment + 2, c->remote_port);
    put_be32(segment + 4, seq);
    put_be32(segment + 8, (flags & FLAG_ACK) ? c->rcv_nxt : 0);
    if (flags & FLAG_SYN) {             /* Option: unsere maximale Segmentgröße */
        segment[20] = 2;
        segment[21] = 4;
        put_be16(segment + 22, MSS_OURS);
        header += 4;
    }
    segment[12] = (uint8_t)((header / 4) << 4);
    segment[13] = flags;
    uint32_t window = c->rx ? RX_BUFFER - c->rx_count : 0;
    put_be16(segment + 14, (uint16_t)window);
    put_be16(segment + 16, 0);
    put_be16(segment + 18, 0);
    if (len)
        memcpy(segment + header, data, len);

    /* Prüfsumme über einen gedachten Vorspann mit den IP-Adressen */
    uint32_t total = header + len;
    ip4_t local = net_local_ip();
    uint32_t pseudo = (local >> 16) + (local & 0xFFFF) + (c->remote >> 16) +
                      (c->remote & 0xFFFF) + IP_PROTO_TCP + total;
    put_be16(segment + 16, net_checksum(segment, total, pseudo));
    ip_send(c->remote, IP_PROTO_TCP, segment, total);
}

static void send_ack(tcp_conn_t *c)
{
    send_segment(c, c->snd_una + c->tx_sent + (c->fin_sent ? 1 : 0), FLAG_ACK,
                 NULL, 0);
}

/* Schickt, was im Sendepuffer wartet und ins Fenster passt. */
static void output(tcp_conn_t *c)
{
    if (c->state != TCP_OPEN)
        return;

    uint32_t limit = c->snd_wnd < FLIGHT_MAX ? c->snd_wnd : FLIGHT_MAX;
    if (limit < c->mss && c->tx_sent == 0)
        limit = c->mss;                 /* nie ganz stehen bleiben */
    while (c->tx_sent < c->tx_len && c->tx_sent < limit) {
        uint32_t n = c->tx_len - c->tx_sent;
        if (n > c->mss)
            n = c->mss;
        if (n > limit - c->tx_sent)
            n = limit - c->tx_sent;
        send_segment(c, c->snd_una + c->tx_sent, FLAG_ACK | FLAG_PSH,
                     c->tx + c->tx_sent, n);
        c->tx_sent += n;
    }
    if (c->fin_queued && !c->fin_sent && c->tx_sent == c->tx_len) {
        send_segment(c, c->snd_una + c->tx_sent, FLAG_ACK | FLAG_FIN, NULL, 0);
        c->fin_sent = true;
    }
    bool outstanding = c->tx_sent > 0 || (c->fin_sent && !c->fin_acked);
    if (outstanding && !c->rto_at)
        c->rto_at = time_ms() + c->rto_ms;
}

static void kill_connection(tcp_conn_t *c)
{
    c->state = TCP_DEAD;
    c->rto_at = 0;
    if (c->waiter)
        task_wake(c->waiter);
}

/* --- Empfangen ----------------------------------------------------------------------- */

static void reply_reset(ip4_t src, const uint8_t *seg, uint32_t len,
                        uint32_t header)
{
    /* Segment für eine Verbindung, die es nicht gibt: höflich ablehnen */
    if (seg[13] & FLAG_RST)
        return;
    tcp_conn_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.remote = src;
    tmp.local_port = get_be16(seg + 2);
    tmp.remote_port = get_be16(seg);
    uint32_t seq_len = len - header + ((seg[13] & (FLAG_SYN | FLAG_FIN)) ? 1 : 0);
    tmp.rcv_nxt = get_be32(seg + 4) + seq_len;
    uint32_t seq = (seg[13] & FLAG_ACK) ? get_be32(seg + 8) : 0;
    send_segment(&tmp, seq, FLAG_RST | FLAG_ACK, NULL, 0);
}

void tcp_input(ip4_t src, const uint8_t *seg, size_t len)
{
    if (len < TCP_HEADER)
        return;
    uint32_t header = (uint32_t)(seg[12] >> 4) * 4;
    if (header < TCP_HEADER || header > len)
        return;

    ip4_t local = net_local_ip();
    uint32_t pseudo = (local >> 16) + (local & 0xFFFF) + (src >> 16) +
                      (src & 0xFFFF) + IP_PROTO_TCP + (uint32_t)len;
    if (net_checksum(seg, len, pseudo) != 0)
        return;                         /* unterwegs beschädigt */

    uint16_t src_port = get_be16(seg);
    uint16_t dst_port = get_be16(seg + 2);
    uint32_t seq = get_be32(seg + 4);
    uint32_t ack = get_be32(seg + 8);
    uint8_t flags = seg[13];
    const uint8_t *data = seg + header;
    uint32_t data_len = (uint32_t)len - header;

    tcp_conn_t *c = NULL;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        tcp_conn_t *k = &connections[i];
        if ((k->state == TCP_SYN_SENT || k->state == TCP_OPEN) &&
            k->remote == src && k->remote_port == src_port &&
            k->local_port == dst_port)
            c = k;
    }
    if (!c) {
        reply_reset(src, seg, (uint32_t)len, header);
        return;
    }

    if (flags & FLAG_RST) {
        kill_connection(c);
        return;
    }

    if (c->state == TCP_SYN_SENT) {
        if ((flags & (FLAG_SYN | FLAG_ACK)) != (FLAG_SYN | FLAG_ACK) ||
            ack != c->snd_una + 1)
            return;
        /* Optionen nach der Segmentgröße der Gegenseite durchsuchen */
        for (uint32_t i = TCP_HEADER; i + 1 < header; ) {
            uint8_t kind = seg[i];
            if (kind == 0)
                break;
            if (kind == 1) {
                i++;
                continue;
            }
            uint8_t size = seg[i + 1];
            if (size < 2 || i + size > header)
                break;
            if (kind == 2 && size == 4) {
                uint32_t mss = get_be16(seg + i + 2);
                c->mss = mss < MSS_OURS ? mss : MSS_OURS;
            }
            i += size;
        }
        c->snd_una = ack;
        c->rcv_nxt = seq + 1;
        c->snd_wnd = get_be16(seg + 14);
        c->state = TCP_OPEN;
        c->rto_at = 0;
        c->rto_ms = RTO_INITIAL;
        c->retries = 0;
        send_ack(c);
        if (c->waiter)
            task_wake(c->waiter);
        return;
    }

    /* --- Verbindung steht: Bestätigungen auswerten --- */
    if (flags & FLAG_ACK) {
        uint32_t acked = ack - c->snd_una;
        uint32_t in_flight = c->tx_sent + (c->fin_sent && !c->fin_acked ? 1 : 0);
        if (acked > 0 && acked <= in_flight) {
            uint32_t bytes = acked;
            if (bytes > c->tx_sent) {   /* auch unser FIN ist bestätigt */
                bytes = c->tx_sent;
                c->fin_acked = true;
            }
            memmove(c->tx, c->tx + bytes, c->tx_len - bytes);
            c->tx_len -= bytes;
            c->tx_sent -= bytes;
            c->snd_una += bytes;
            c->rto_at = 0;
            c->rto_ms = RTO_INITIAL;
            c->retries = 0;
            if (c->waiter)
                task_wake(c->waiter);
        }
        c->snd_wnd = get_be16(seg + 14);
    }

    /* --- Daten und Verbindungsende der Gegenseite --- */
    bool fin = flags & FLAG_FIN;
    if (data_len > 0 || fin) {
        /* Schon bekannten Anfang abschneiden (erneut gesendete Daten) */
        uint32_t offset = c->rcv_nxt - seq;
        if (offset <= data_len && !c->peer_fin) {
            data += offset;
            data_len -= offset;
            uint32_t space = RX_BUFFER - c->rx_count;
            if (data_len > space) {
                data_len = space;
                fin = false;            /* Rest kommt später noch einmal */
            }
            uint32_t tail = (c->rx_head + c->rx_count) % RX_BUFFER;
            uint32_t first = RX_BUFFER - tail < data_len ? RX_BUFFER - tail
                                                         : data_len;
            memcpy(c->rx + tail, data, first);
            memcpy(c->rx, data + first, data_len - first);
            c->rx_count += data_len;
            c->rcv_nxt += data_len;
            if (fin) {
                c->peer_fin = true;
                c->rcv_nxt++;
            }
            if (c->waiter && (data_len > 0 || fin))
                task_wake(c->waiter);
        }
        send_ack(c);                    /* auch bei Lücken: sagt, was fehlt */
    }
    output(c);
}

void tcp_timer(void)
{
    uint64_t now = time_ms();
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        tcp_conn_t *c = &connections[i];
        if (!c->rto_at || now < c->rto_at)
            continue;
        if (c->state != TCP_SYN_SENT && c->state != TCP_OPEN) {
            c->rto_at = 0;
            continue;
        }
        if (++c->retries > RETRIES_MAX) {
            kill_connection(c);
            continue;
        }
        /* Keine Bestätigung: Wartezeit verdoppeln und erneut senden */
        c->rto_ms *= 2;
        c->rto_at = now + c->rto_ms;
        if (c->state == TCP_SYN_SENT) {
            send_segment(c, c->snd_una, FLAG_SYN, NULL, 0);
        } else {
            c->tx_sent = 0;
            c->fin_sent = false;
            output(c);
        }
    }
}

/* --- Schnittstelle für Programme ------------------------------------------------------ */

tcp_conn_t *tcp_connect(ip4_t ip, uint16_t port, uint32_t timeout_ms)
{
    uint8_t *rx = kmalloc(RX_BUFFER);
    uint8_t *tx = kmalloc(TX_BUFFER);
    if (!rx || !tx) {
        kfree(rx);
        kfree(tx);
        return NULL;
    }

    uint64_t flags = irq_save();
    tcp_conn_t *c = NULL;
    for (int i = 0; i < MAX_CONNECTIONS && !c; i++) {
        if (connections[i].state == TCP_FREE)
            c = &connections[i];
    }
    if (!c || !net_local_ip()) {
        irq_restore(flags);
        kfree(rx);
        kfree(tx);
        return NULL;
    }
    if (next_port < 49152)
        next_port = (uint16_t)(49152 + rdtsc() % 16000);

    memset(c, 0, sizeof(*c));
    c->rx = rx;
    c->tx = tx;
    c->remote = ip;
    c->remote_port = port;
    c->local_port = next_port++;
    c->snd_una = (uint32_t)rdtsc();     /* zufällige Startnummer */
    c->mss = MSS_DEFAULT;
    c->rto_ms = RTO_INITIAL;
    c->state = TCP_SYN_SENT;
    send_segment(c, c->snd_una, FLAG_SYN, NULL, 0);
    c->rto_at = time_ms() + c->rto_ms;

    uint64_t deadline = time_ms() + timeout_ms;
    while (c->state == TCP_SYN_SENT && time_ms() < deadline)
        net_wait(&c->waiter, deadline);

    if (c->state != TCP_OPEN) {
        c->state = TCP_FREE;
        c->rto_at = 0;
        irq_restore(flags);
        kfree(rx);
        kfree(tx);
        return NULL;
    }
    irq_restore(flags);
    return c;
}

bool tcp_send(tcp_conn_t *c, const void *data, size_t len)
{
    const uint8_t *p = data;
    uint64_t flags = irq_save();

    while (len > 0 && c->state == TCP_OPEN && !c->fin_queued) {
        uint32_t space = TX_BUFFER - c->tx_len;
        if (space == 0) {
            net_wait(&c->waiter, time_ms() + 1000);
            continue;
        }
        uint32_t n = len < space ? (uint32_t)len : space;
        memcpy(c->tx + c->tx_len, p, n);
        c->tx_len += n;
        p += n;
        len -= n;
        output(c);
    }
    /* Warten, bis die Gegenseite alles bestätigt hat */
    while (c->state == TCP_OPEN && c->tx_len > 0)
        net_wait(&c->waiter, time_ms() + 1000);

    bool ok = len == 0 && c->state == TCP_OPEN;
    irq_restore(flags);
    return ok;
}

int tcp_recv(tcp_conn_t *c, void *buffer, size_t max, uint32_t timeout_ms)
{
    uint64_t deadline = time_ms() + timeout_ms;
    uint64_t flags = irq_save();

    while (c->rx_count == 0 && !c->peer_fin && c->state == TCP_OPEN &&
           time_ms() < deadline)
        net_wait(&c->waiter, deadline);

    int result;
    if (c->rx_count > 0) {
        uint32_t before = RX_BUFFER - c->rx_count;
        uint32_t n = c->rx_count < max ? c->rx_count : (uint32_t)max;
        uint32_t first = RX_BUFFER - c->rx_head < n ? RX_BUFFER - c->rx_head : n;
        memcpy(buffer, c->rx + c->rx_head, first);
        memcpy((uint8_t *)buffer + first, c->rx, n - first);
        c->rx_head = (c->rx_head + n) % RX_BUFFER;
        c->rx_count -= n;
        /* War das Fenster fast zu, der Gegenseite sagen, dass wieder Platz ist */
        if (before < 2 * MSS_OURS && c->state == TCP_OPEN)
            send_ack(c);
        result = (int)n;
    } else if (c->peer_fin) {
        result = 0;
    } else {
        result = -1;
    }
    irq_restore(flags);
    return result;
}

void tcp_close(tcp_conn_t *c)
{
    if (!c)
        return;
    uint64_t flags = irq_save();
    if (c->state == TCP_OPEN) {
        /* Ordentlich verabschieden und kurz auf die Bestätigung warten */
        c->fin_queued = true;
        output(c);
        uint64_t deadline = time_ms() + 1500;
        while (c->state == TCP_OPEN && !c->fin_acked && time_ms() < deadline)
            net_wait(&c->waiter, deadline);
    }
    uint8_t *rx = c->rx, *tx = c->tx;
    c->rx = NULL;
    c->tx = NULL;
    c->rto_at = 0;
    c->state = TCP_FREE;
    irq_restore(flags);
    kfree(rx);
    kfree(tx);
}
