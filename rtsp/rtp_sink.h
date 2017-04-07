#ifndef _RTP_SINK_H_
#define _RTP_SINK_H_

#include "avcodec.h"
#include "rtp_common.h"

#define SINK_FLAG_FRAME_BY_REF		0x01
#define SINK_FLAG_FRAME_BY_BUF		0x02
#define SINK_FLAG_UNSPECIFIED		0x00

//structure for sending data
typedef struct _rtp_sink{
	u32 ssrc;
	u32 base_ts; //base timestamp
	u32 now_ts;
	u16 seq_no;
	u8 codec_id;
	char codec_name[8];
	u8 media_type;
	u8 pt;//payload type
	u8 nb_channels;
	u32 frequency;
	u8 frame_rate;
	u32 bit_rate;	
	u32 packet_cnt;
	u32 octet_cnt;
	u32 total_octet_cnt;
	u8 sink_flag;
	struct rtp_packet *packet;
	struct avcodec_handle_opts *media_hdl_opts;	
	rtp_trans_stats *stats; 
	//rtcp_instance *rtcp_inst;
}rtp_sink_t;


rtp_sink_t *rtp_sink_create_by_codec_id(u8 codec_id);
rtp_sink_t *rtp_sink_create_by_codec_name(const char* name);
int rtp_sink_is_frame_by_ref(rtp_sink_t *sink);
int rtp_sink_is_frame_by_buf(rtp_sink_t *sink);
void rtp_sink_set_frame_by_ref(rtp_sink_t *sink);
void rtp_sink_set_frame_by_buf(rtp_sink_t *sink);
int rtp_sink_packet_init(rtp_sink_t *sink, int buf_size);
int rtp_sink_stats_init(rtp_sink_t *sink);
int rtp_sink_rtcp_init(rtp_sink_t *sink);

#endif