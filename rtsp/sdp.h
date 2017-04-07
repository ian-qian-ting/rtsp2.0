#ifndef _SDP_H_
#define _SDP_H_

#include "avcodec.h"

#define CRLF "\r\n"
#define IS_LINE_END(x) (return *(x)=='\r' || *(x)=='\0')
#define MAX_SDP_SIZE (512+256)
#define SDP_LINE_LEN (128+256)

#define SDP_BWTYPE_CT 0
#define SDP_BWTYPE_AS 1
//RS RR are newly registered, to be supported in future
#define SDP_BWTYPE_RS 2
#define SDP_BWTYPE_RR 3

#define SDP_TYPE_BROADCAST 0
#define SDP_TYPE_MEETING 1
#define SDP_TYPE_MODERATED 2
#define SDP_TYPE_TEST 3
#define SDP_TYPE_H332 4

void sdp_strcat(unsigned char *buf1, int size, unsigned char *buf2);
void sdp_fill_o_field(unsigned char *sdp_buf, int size, u8 *username, u32 session_id, u8 session_version, u8* nettype, u8* addrtype, u8* unicast_addr);
void sdp_fill_s_field(unsigned char *sdp_buf, int size, u8 * session_name);
void sdp_fill_i_field(unsigned char *sdp_buf, int size, u8 * session_info);
void sdp_fill_u_field(unsigned char * sdp_buf, int size, u8 *uri);
void sdp_fill_c_field(unsigned char *sdp_buf, int size, u8 *nettype, u8 *addrtype, u8 *connection_addr, u8 ttl);
void sdp_fill_b_field(unsigned char *sdp_buf, int size, int bwtype, int bw);
void sdp_fill_t_field(unsigned char *sdp_buf, int size, u64 start_time, u64 end_time);
void sdp_fill_m_field(unsigned char *sdp_buf, int size, int media_type, u16 port, int fmt);
void sdp_fill_a_string(unsigned char *sdp_buf, int size, u8 *string);
void sdp_fill_a_rtpmap(unsigned char *sdp_buf, int size, struct codec_info *codec);


#endif