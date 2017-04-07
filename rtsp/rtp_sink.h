#ifndef _RTP_SINK_H_
#define _RTP_SINK_H_

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

//structure for sending data
struct rtp_sink{
	u32 ssrc;
	u32 base_ts; //base timestamp
	u32 now_ts;
	u32 frequency;
	u8 pt;//payload type
	u8 nb_channels;
	u16 seq_no;
	u32 bit_rate;
	u32 packet_cnt;
	u32 octet_cnt;
	u32 total_octet_cnt;
	rtp_trans_stats *stats; 
	rtcp_instance *rtcp_inst;
};


#endif