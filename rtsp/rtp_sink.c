#include "FreeRTOS.h"
#include "platform/platform_stdlib.h"
#include "osdep_service.h"
#include "avcodec.h"
#include "avcodec_util.h"
#include "rtp_sink.h"
#include "rtsp_rtp_dbg.h"

int rtp_sink_init_by_codec_id(rtp_sink_t *sink, u8 codec_id)
{
	if(codec_id > AV_CODEC_ID_LAST_ONE)
	{
		RTP_ERROR("failed to create sink: unsupported codec id!");
		return -EINVAL;
	}
	//memset(sink, 0, sizeof(rtp_sink_t));
	switch(codec_id)
	{
		case(AV_CODEC_ID_MJPEG):
			sink->codec_id = codec_id;
                        memcpy(sink->codec_name, "MJPEG", 5);
			sink->media_type = AVMEDIA_TYPE_VIDEO;
			sink->pt = RTP_PT_JPEG;
			sink->frequency = 90000;
			//sink->media_hdl_opts = mjpeg_hdl_opts;
			break;
		case(AV_CODEC_ID_H264):
			sink->codec_id = codec_id;
                        memcpy(sink->codec_name, "H264", 4);
			sink->media_type = AVMEDIA_TYPE_VIDEO;
			sink->pt = RTP_PT_DYN_BASE;
			sink->frequency = 90000;
			//sink->media_hdl_opts = h264_hdl_opts;
			break;
		case(AV_CODEC_ID_PCMU):
			sink->codec_id = codec_id;
                        memcpy(sink->codec_name, "PCMU", 4);
			sink->media_type = AVMEDIA_TYPE_AUDIO;
			sink->pt = RTP_PT_PCMU;
			sink->frequency = 8000;
			sink->nb_channels = 1;
			//sink->media_hdl_opts = g711_hdl_opts;
			break;
		case(AV_CODEC_ID_PCMA):
			sink->codec_id = codec_id;
                        memcpy(sink->codec_name, "PCMA", 4);
			sink->media_type = AVMEDIA_TYPE_AUDIO;
			sink->pt = RTP_PT_PCMA;
			sink->frequency = 8000;
			sink->nb_channels = 1;
			//sink->media_hdl_opts = g711_hdl_opts;
			break;
		case(AV_CODEC_ID_MP4A_LATM):
			sink->codec_id = codec_id;
                        memcpy(sink->codec_name, "MP4A", 4);
			sink->media_type = AVMEDIA_TYPE_AUDIO;
			sink->pt = RTP_PT_DYN_BASE;
			sink->frequency = 16000;
			sink->nb_channels = 2;
			//sink->media_hdl_opts = aac_hdl_opts;
			break;
		case(AV_CODEC_ID_MP4V_ES):
			sink->codec_id = codec_id;
                        memcpy(sink->codec_name, "MP4V", 4);
			sink->media_type = AV_CODEC_ID_MP4V_ES;
			sink->pt = RTP_PT_DYN_BASE;
			sink->frequency = 90000;
                        //sink->media_hdl_opts = ...;
			break;
		default:
			break;
	}
	return 0;
}

int rtp_sink_init_by_codec_name(rtp_sink_t *sink, const char* name)
{
	u8 codec_id = AV_CODEC_ID_UNKNOWN;
	if(!strncmp(name, "MJPEG", 5))
		codec_id = AV_CODEC_ID_MJPEG;
	else if(!strncmp(name, "H264", 4))
		codec_id = AV_CODEC_ID_H264;
		else if(!strncmp(name, "PCMU", 4))
			codec_id = AV_CODEC_ID_PCMU;
			else if(!strncmp(name, "PCMA", 4))
				codec_id = AV_CODEC_ID_PCMA;
				else if(!strncmp(name, "MP4A", 4))
					codec_id = AV_CODEC_ID_MP4A_LATM;
					else if(!strncmp(name, "MP4V", 4))
						codec_id = AV_CODEC_ID_MP4V_ES;
	return rtp_sink_init_by_codec_id(sink, codec_id);
}


int rtp_sink_is_frame_by_ref(rtp_sink_t *sink)
{
	return (sink->sink_flag & SINK_FLAG_FRAME_BY_REF);
}

int rtp_sink_is_frame_by_buf(rtp_sink_t *sink)
{
	return (sink->sink_flag & SINK_FLAG_FRAME_BY_BUF);
}

void rtp_sink_set_frame_by_ref(rtp_sink_t *sink)
{
	sink->sink_flag &= ~SINK_FLAG_FRAME_BY_BUF;
	sink->sink_flag |= SINK_FLAG_FRAME_BY_REF;
}

int rtp_sink_get_frame(rtp_sink_t *sink, int index, u8 *src, int len)
{
	struct rtp_packet *pckt = sink->packet;
	if(rtp_sink_is_frame_by_buf(sink))
	{
		//hook data by copying to internal buffer
		pckt->index = index;
		//part of data will drop if len > packet buffer len
		pckt->len = (len > pckt->buf_len)?pckt->buf_len:len;
		memcpy(pckt->buf, src, pckt->len);
	}else{
		//hook data by reference of external buffer
		pckt->index = index;
		pckt->data = src;
		pckt->len = len;
	}
	return 0;
}

int rtp_sink_update_ts(rtp_sink_t *sink, u32 ts)
{
	sink->now_ts = ts;
	return 0;
}

void rtp_sink_set_frame_by_buf(rtp_sink_t *sink)
{
	sink->sink_flag &= ~SINK_FLAG_FRAME_BY_REF;
	sink->sink_flag |= SINK_FLAG_FRAME_BY_BUF;
}

int rtp_sink_packet_init(rtp_sink_t *sink, int buf_size)
{
	struct rtp_packet *pckt = malloc(sizeof(struct rtp_packet));
	if(pckt == NULL)
	{
		RTP_ERROR("allocate sink packet failed");
		return -1;
	}
	rtw_init_sema(&pckt->sema_ref, 0);
	if(buf_size > 0)
	{
		if((pckt->buf = malloc(sizeof(buf_size))) != NULL)
		{
			pckt->buf_len = buf_size;
			sink->packet = pckt;
			rtp_sink_set_frame_by_buf(sink);
			return 0;
		}
		RTP_ERROR("prepare internal buffer failed");
		RTP_ERROR("switch to frame_by_ref setting");
		//will fall into frame_by_ref case
	}
	sink->packet = pckt;
	rtp_sink_set_frame_by_ref(sink);
	return 0;
}

//signal that frame is sent
void rtp_sink_ind_frame_sent(rtp_sink_t *sink)
{
	if(rtp_sink_is_frame_by_ref(sink))
		rtw_up_sema(&sink->packet->sema_ref);
}

//return 0 means frame has been sent
int rtp_sink_wait_frame_sent(rtp_sink_t *sink, u32 wait_time)
{
	if(rtp_sink_is_frame_by_ref(sink)) //wait until data has been consumed
	{
		if(rtw_down_timeout_sema(&sink->packet->sema_ref, (wait_time==0)? RTW_MAX_DELAY : wait_time) == 0)//fail to take sema
			return -1;
	}else{
		if(wait_time != 0 && wait_time != RTW_MAX_DELAY)
			rtw_msleep_os(wait_time);
	}
	return 0;
}

int rtp_sink_stats_init(rtp_sink_t *sink)
{
	
}

int rtp_sink_rtcp_init(rtp_sink_t *sink)
{
	
}