#include "outer_header.h"
#include <string.h>

/* Helpers to write big-endian values without <arpa/inet.h> dependency. */
static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

void build_outer_header_struct(int path_id, OuterHeader *hdr)
{
    if (!hdr)
        return;
    if (path_id != 1 && path_id != 2)
        path_id = 1;

    /* Ethernet II */
    static const uint8_t dmac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
    static const uint8_t smac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 };
    memcpy(hdr->dst_mac, dmac, 6);
    memcpy(hdr->src_mac, smac, 6);
    /* ethertype 0x0800 in wire (big-endian) order */
    put_be16((uint8_t *)&hdr->ethertype, (uint16_t)0x0800);

    /* IPv4 */
    hdr->ver_ihl = 0x45;
    hdr->dscp_ecn = 0x00;
    put_be16((uint8_t *)&hdr->total_length, (uint16_t)836);
    put_be16((uint8_t *)&hdr->identification, (uint16_t)0x0000);
    put_be16((uint8_t *)&hdr->flags_frag, (uint16_t)0x4000); /* DF */
    hdr->ttl = 64;
    hdr->protocol = 17;
    put_be16((uint8_t *)&hdr->hdr_checksum, (uint16_t)0x0000);
    put_be32((uint8_t *)&hdr->src_ip, 0x0A000001u); /* 10.0.0.1 */
    if (path_id == 1)
        put_be32((uint8_t *)&hdr->dst_ip, 0xC0A80A65u); /* 192.168.10.101 */
    else
        put_be32((uint8_t *)&hdr->dst_ip, 0xC0A81466u); /* 192.168.20.102 */

    /* UDP */
    put_be16((uint8_t *)&hdr->sport, (uint16_t)(50001 + (path_id - 1)));
    put_be16((uint8_t *)&hdr->dport, (uint16_t)OUTER_UDP_DPORT);
    put_be16((uint8_t *)&hdr->udp_len, (uint16_t)816);
    put_be16((uint8_t *)&hdr->udp_checksum, (uint16_t)0x0000);

    /* GTP-U */
    hdr->gtp_flags = 0x30;
    hdr->gtp_type = OUTER_GTPU_TYPE;
    put_be16((uint8_t *)&hdr->gtp_length, (uint16_t)OUTER_PAYLOAD_LEN);
    if (path_id == 1)
        put_be32((uint8_t *)&hdr->teid, 0x10000001u);
    else
        put_be32((uint8_t *)&hdr->teid, 0x20000002u);
}

void build_outer_header(int path_id, uint8_t out[OUTER_HEADER_LEN])
{
    if (!out)
        return;
    OuterHeader hdr;
    build_outer_header_struct(path_id, &hdr);
    memcpy(out, &hdr, OUTER_HEADER_LEN);
}

uint16_t outer_header_len(void)
{
    return OUTER_HEADER_LEN;
}
