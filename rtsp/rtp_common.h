#ifndef _RTP_COMMON_H_
#define _RTP_COMMON_H_

/**************************************************INCLUDE*****************************************************/

#include "dlist.h"
#include "basic_types.h"
#include "osdep_service.h"

/**************************************************DEFINITIONS*************************************************/

#if (SYSTEM_ENDIAN==PLATFORM_LITTLE_ENDIAN)
#define RTP_BIG_ENDIAN 0
#else
#define RTP_BIG_ENDIAN 1
#endif

#define RTP_HDR_SZ 12

#define RTP_SERVER_PORT_BASE 55608
#define RTP_SERVER_PORT_RANGE 1000
#define RTP_PORT_BASE 50020
#define RTP_PORT_RANGE 1000
#define RTP_CLIENT_PORT_BASE 51020
#define RTP_CLIENT_PORT_RANGE 1000

/*define rtp packet status*/
#define RTP_PCKT_IDLE           0x00 
#define RTP_PCKT_READY          0x01
#define RTP_PCKT_PROCESS        0x02
#define RTP_PCKT_REF            0x40
#define RTP_PCKT_INT            0x80

/**************************************************STURCTURES**************************************************/

/*
 * RTP data header from RFC1889
 */
/*
 *
 *
 *    The RTP header has the following format:
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |V=2|P|X|  CC   |M|     PT      |       sequence number         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                           timestamp                           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           synchronization source (SSRC) identifier            |
 * +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
 * |            contributing source (CSRC) identifiers             |
 * |                             ....                              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * RTP data header
 */


RTW_PACK_STRUCT_BEGIN 
typedef struct {
#if RTP_BIG_ENDIAN
        u16 version:2;   /* protocol version */
        u16 p:1;         /* padding flag */
        u16 x:1;         /* header extension flag */
        u16 cc:4;        /* CSRC count */
        u16 m:1;         /* marker bit */
        u16 pt:7;        /* payload type */
#else /*RTP_LITTLE_ENDIAN*/
        u16 cc:4;        /* CSRC count */
        u16 x:1;         /* header extension flag */
        u16 p:1;         /* padding flag */
        u16 version:2;   /* protocol version */
        u16 pt:7;        /* payload type */
        u16 m:1;         /* marker bit */
#endif
        u16 seq;              /* sequence number */
        u32 ts;               /* timestamp */
        u32 ssrc;             /* synchronization source */
        u32 *csrc;          /* optional CSRC list, skip if cc is set to 0 here*/
} rtp_hdr_t;
RTW_PACK_STRUCT_END

struct _rtp_sink;
struct _rtp_source;

struct rtp_packet
{
	rtp_hdr_t rtphdr;
	void *extra; //pointer to media type specific structure	
	int index; //internal buffer index if we get frame by ref instead of by copy
	u8 *data; //pointer to sink data by ref
	int len; //actual data len;
	_mutex lock;
        u8 status;
};

typedef struct _rtp_trans_stats{
	u32 ssrc;
	//from addr?
	u32 last_packet_recv;
	u8 packet_lost_ratio;
	u32 nb_packet_lost;
	u32 jitter;
	u32 last_SR_time;
	u32 dif_SR_RR_time;
	//time created? time received?
	u32 prev_last_packet_recv;
	u32 prev_nb_packet_lost;
	u8 is_first_packet;
	//... TBD
}rtp_trans_stats;

#if 0
typedef struct _rtp_recv_stats{

}rtp_recv_stats;
#endif

/**************************************************DECLARATIONS************************************************/

void rtp_fill_header(rtp_hdr_t *rtphdr, int version, int padding, int extension, int cc, int marker, int pt, u16 seq, u32 ts, u32 ssrc);
int rtp_parse_header(u8 *src, rtp_hdr_t *rtphdr, int is_nbo);
#endif