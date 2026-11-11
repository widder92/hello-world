/* Netzwerk: Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS und TCP */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* IPv4-Adressen als Zahl: 10.0.2.15 = 0x0A00020F. */
typedef uint32_t ip4_t;
#define IP4(a, b, c, d) \
    (((ip4_t)(a) << 24) | ((ip4_t)(b) << 16) | ((ip4_t)(c) << 8) | (ip4_t)(d))

/* --- Netzwerkkarten ---------------------------------------------------------- */

#define ETH_FRAME_MAX 1514              /* ohne Prüfsumme */

typedef struct nic {
    const char *name;
    uint8_t mac[6];
    bool (*link_up)(struct nic *self);
    bool (*send)(struct nic *self, const void *frame, size_t len);
    /* Holt ein empfangenes Paket ab; liefert dessen Länge oder 0. */
    size_t (*receive)(struct nic *self, void *buffer, size_t max);
    /* Zähler der Karte als Text, für die Fehlersuche (darf fehlen). */
    void (*stats)(struct nic *self, char *text, size_t size);
    void *driver_data;
} nic_t;

/* Sucht eine unterstützte Intel-Netzwerkkarte (e1000-Familie). */
nic_t *e1000_probe(void);
/* Sucht Intels neuere Chips: I225/I226 (2,5 Gbit) sowie 82576, I210, I211. */
nic_t *igc_probe(void);

/* --- Zustand ------------------------------------------------------------------ */

typedef struct {
    bool present;                       /* Netzwerkkarte gefunden */
    bool link;                          /* Kabel steckt */
    bool configured;                    /* Adresse per DHCP erhalten */
    const char *driver;
    uint8_t mac[6];
    ip4_t ip, netmask, gateway, dns;
    uint64_t rx_packets, tx_packets;
    char card_stats[280];               /* Zähler der Karte, falls sie welche führt */
} net_info_t;

/* Netzwerkkarte suchen und den Netzwerk-Task starten. */
void net_init(void);
void net_get_info(net_info_t *out);
void net_set_dns(ip4_t server);

/* Wartet (höchstens timeout_ms), bis eine Adresse zugeteilt wurde. */
bool net_wait_configured(uint32_t timeout_ms);

/* "10.0.2.2" <-> Zahl */
bool ip4_parse(const char *text, ip4_t *out);
void ip4_format(ip4_t ip, char out[16]);

/* --- Dienste ------------------------------------------------------------------- */

/* Schickt ein ICMP-Echo und wartet auf die Antwort. */
bool net_ping(ip4_t target, uint32_t timeout_ms, uint32_t *rtt_ms);

/* Löst einen Namen per DNS auf (nimmt auch fertige IPv4-Adressen an). */
bool net_resolve(const char *name, ip4_t *out);

/* --- TCP (nur ausgehende Verbindungen) ------------------------------------------- */

typedef struct tcp_conn tcp_conn_t;

tcp_conn_t *tcp_connect(ip4_t ip, uint16_t port, uint32_t timeout_ms);

/* Sendet alles und wartet auf die Bestätigung der Gegenseite. */
bool tcp_send(tcp_conn_t *conn, const void *data, size_t len);

/*
 * Empfängt bis zu max Bytes. Liefert die Anzahl, 0 wenn die Gegenseite die
 * Verbindung ordentlich beendet hat, -1 bei Fehler oder Zeitüberschreitung.
 */
int tcp_recv(tcp_conn_t *conn, void *buffer, size_t max, uint32_t timeout_ms);
void tcp_close(tcp_conn_t *conn);

/* --- Intern: Schnittstelle zwischen net.c und tcp.c ----------------------------- */

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

bool ip_send(ip4_t dst, uint8_t protocol, const void *payload, size_t len);
ip4_t net_local_ip(void);
uint16_t net_checksum(const void *data, size_t len, uint32_t initial);
void net_wait(uint32_t *waiter, uint64_t deadline_ms);  /* mit gesperrten IRQs */
void tcp_input(ip4_t src, const uint8_t *segment, size_t len);
void tcp_timer(void);

static inline uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static inline void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static inline void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
