#ifndef _RTP_SRC_H_
#define _RTP_SRC_H_



//structure for receiving data
typedef struct _rtp_source{
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
	struct avcodec_hdl_opts *media_hdl_opts;	
	//rtp_recv_stats *stats; 
	//rtcp_instance *rtcp_inst;
}rtp_source_t;

#endif