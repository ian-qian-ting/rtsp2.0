#ifndef _RTCP_API_H_
#define _RTCP_API_H_

#include "dlist.h"
#include "basic_types.h"
#include "osdep_service.h"

#if (SYSTEM_ENDIAN==PLATFORM_LITTLE_ENDIAN)
#define RTCP_BIG_ENDIAN 0
#else
#define RTCP_BIG_ENDIAN 1
#endif

#define RTCP_TYPE_SR 200
#define RTCP_TYPE_RR 201
#define RTCP_TYPE_SDES 202
#define RTCP_TYPE_BYE 203
#define RTCP_TYPE_APP 204

typedef struct {
#if RTCP_BIG_ENDIAN
  unsigned int count:5;         /* varies by packet type */
  unsigned int padding:1;       /* padding flag */
  unsigned int version:2;       /* protocol version */
#else
  unsigned int version:2;       /* protocol version */
  unsigned int padding:1;       /* padding flag */
  unsigned int count:5;     /* varies by packet type */
#endif
  unsigned int packet_type:8;   /* RTCP packet type */
  u16 length;               /* pkt len in words, w/o this word */
}rtcp_hdr_t;

//report info
typedef struct {
  u32 ssrc;                 /* data source being reported */
  unsigned int fraction:8;      /* fraction lost since last SR/RR */
  int lost:24;                  /* cumul. no. pkts lost (signed!) */
  u32 last_seq;             /* extended last seq. no. received */
  u32 jitter;               /* interarrival jitter */
  u32 lsr;                  /* last SR packet from this source */
  u32 dlsr;                 /* delay since last SR packet */
}rtcp_rr_t;

typedef struct {
  u32 ssrc;                 /* sender generating this report */
  u32 ntp_sec;              /* NTP timestamp */
  u32 ntp_frac;
  u32 rtp_ts;               /* RTP timestamp */
  u32 psent;                /* packets sent */
  u32 osent;                /* octets sent */
  rtcp_rr_t rr[1];         /* variable-length list */
}rtcp_sr;

typedef struct {
  u32 ssrc;                 /* receiver generating this report */
  rtcp_rr_t rr[1];         /* variable-length list */
}rtcp_rr;

typedef struct {
  u8 type;                  /* type of item (Rtcp_Sdes_Type) */
  u8 length;                /* length of item (in octets) */
  char data[1];                /* text, not null-terminated */
}rtcp_sdes_item_t;

typedef struct {
  u32 src;                  /* first SSRC/CSRC */
  rtcp_sdes_item_t item[1];     /* list of SDES items */
}rtcp_sdes;
    
typedef struct {
  u32 src[1];               /* list of sources */
  char data[1];                /* reason for leaving */
}rtcp_bye;

/*
 * One RTCP packet
 */

typedef struct {
  rtcp_hdr_t rtcphdr;
  union{
      rtcp_sr sr;
	  rtcp_rr rr;
	  rtcp_sdes sdes;
	  rtcp_bye bye;
  }r;
}rtcp_packet_t;
 
#if 0
struct Rtcp_APP_Header {
  u32 ssrc;                 /* source */
  char name[4];                /* name */
  char data[1];                /* application data */
};
#endif

#endif