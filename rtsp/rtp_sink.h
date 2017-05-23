#ifndef _RTP_SINK_H_
#define _RTP_SINK_H_

#include "rtp_avcodec/avcodec.h"
#include "rtp_avcodec/avcodec_util.h"
#include "rtp_common.h"

#define SINK_FLAG_FRAME_BY_REF		0x01
#define SINK_FLAG_FRAME_BY_BUF		0x02
#define SINK_FLAG_UNSPECIFIED		0x00

//structure for sending data
typedef struct _rtp_sink{
        int rtp_sock;
        int rtcp_sock;
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
	struct avcodec_handle_ops *media_hdl_ops;	
	rtp_trans_stats *stats; 
	//rtcp_instance *rtcp_inst;
}rtp_sink_t;


int rtp_sink_init_by_codec_id(rtp_sink_t *sink, u8 codec_id);
int rtp_sink_init_by_codec_name(rtp_sink_t *sink, const char* name);
int rtp_sink_is_frame_by_ref(rtp_sink_t *sink);
int rtp_sink_is_frame_by_buf(rtp_sink_t *sink);
void rtp_sink_set_frame_by_none(rtp_sink_t *sink);
void rtp_sink_set_frame_by_ref(rtp_sink_t *sink);
void rtp_sink_set_frame_by_buf(rtp_sink_t *sink);
int rtp_sink_packet_create(rtp_sink_t *sink);        
int rtp_sink_packet_init(rtp_sink_t *sink);
void rtp_sink_packet_free(rtp_sink_t *sink);
void rtp_sink_ind_frame_sent(rtp_sink_t *sink);
int rtp_sink_wait_frame_sent(rtp_sink_t *sink);
void rtp_sink_ind_frame_ready(rtp_sink_t *sink);
int rtp_sink_wait_frame_ready(rtp_sink_t *sink);
void rtp_sink_ind_frame_process(rtp_sink_t *sink);
int rtp_sink_get_frame(rtp_sink_t *sink, int index, u8 *src, int len);
int rtp_sink_stats_init(rtp_sink_t *sink);
int rtp_sink_rtcp_init(rtp_sink_t *sink);

#endif