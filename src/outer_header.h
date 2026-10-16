#ifndef OUTER_HEADER_H
#define OUTER_HEADER_H

#include <stdint.h>

#define OUTER_HEADER_LEN 50
#define OUTER_ETH_LEN 14
#define OUTER_IPV4_LEN 20
#define OUTER_UDP_LEN 8
#define OUTER_GTPU_LEN 8

#define OUTER_UDP_DPORT 2152
#define OUTER_GTPU_TYPE 255
#define OUTER_PAYLOAD_LEN 800

/* Standard 3GPP N3/N9 outer header: Eth(14) + IPv4(20) + UDP(8) + GTP-U(8) = 50B. */
typedef struct __attribute__((packed)) {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;      /* BE 0x0800 */
    /* IPv4 */
    uint8_t  ver_ihl;        /* 0x45 */
    uint8_t  dscp_ecn;
    uint16_t total_length;   /* BE: 20 + 8 + 8 + 800 = 836 */
    uint16_t identification;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;       /* 17 = UDP */
    uint16_t hdr_checksum;
    uint32_t src_ip;         /* BE 10.0.0.1 */
    uint32_t dst_ip;         /* BE Path1 192.168.10.101 / Path2 192.168.20.102 */
    /* UDP */
    uint16_t sport;          /* BE flow hash */
    uint16_t dport;          /* BE 2152 */
    uint16_t udp_len;        /* BE 8 + 8 + 800 = 816 */
    uint16_t udp_checksum;
    /* GTP-U */
    uint8_t  gtp_flags;      /* 0x30: Ver=1, PT=1 */
    uint8_t  gtp_type;       /* 255 = G-PDU */
    uint16_t gtp_length;     /* BE 800 */
    uint32_t teid;           /* BE 0x10000001 / 0x20000002 */
} OuterHeader;

_Static_assert(sizeof(OuterHeader) == OUTER_HEADER_LEN,
               "OuterHeader must be exactly 50 bytes");

void build_outer_header(int path_id, uint8_t out[OUTER_HEADER_LEN]);
void build_outer_header_struct(int path_id, OuterHeader *hdr);
uint16_t outer_header_len(void);

#endif
