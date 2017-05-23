#include "FreeRTOS.h"
#include "platform/platform_stdlib.h"
#include "osdep_service.h"
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
			sink->media_hdl_ops = &mjpeg_hdl_ops;
			break;
		case(AV_CODEC_ID_H264):
			sink->codec_id = codec_id;
                        memcpy(sink->codec_name, "H264", 4);
			sink->media_type = AVMEDIA_TYPE_VIDEO;
			sink->pt = RTP_PT_DYN_BASE;
			sink->frequency = 90000;
			//sink->media_hdl_ops = &h264_hdl_ops;
			break;
		case(AV_CODEC_ID_PCMU):
			sink->codec_id = codec_id;
                        memcpy(sink->codec_name, "PCMU", 4);
			sink->media_type = AVMEDIA_TYPE_AUDIO;
			sink->pt = RTP_PT_PCMU;
			sink->frequency = 8000;
			sink->nb_channels = 1;
			//sink->media_hdl_ops = &g711_hdl_ops;
			break;
		case(AV_CODEC_ID_PCMA):
			sink->codec_id = codec_id;
                        memcpy(sink->codec_name, "PCMA", 4);
			sink->media_type = AVMEDIA_TYPE_AUDIO;
			sink->pt = RTP_PT_PCMA;
			sink->frequency = 8000;
			sink->nb_channels = 1;
			//sink->media_hdl_ops = &g711_hdl_ops;
			break;
		case(AV_CODEC_ID_MP4A_LATM):
			sink->codec_id = codec_id;
                        memcpy(sink->codec_name, "MP4A", 4);
			sink->media_type = AVMEDIA_TYPE_AUDIO;
			sink->pt = RTP_PT_DYN_BASE;
			sink->frequency = 16000;
			sink->nb_channels = 2;
			//sink->media_hdl_ops = &aac_hdl_ops;
			break;
		case(AV_CODEC_ID_MP4V_ES):
			sink->codec_id = codec_id;
                        memcpy(sink->codec_name, "MP4V", 4);
			sink->media_type = AV_CODEC_ID_MP4V_ES;
			sink->pt = RTP_PT_DYN_BASE;
			sink->frequency = 90000;
                        //sink->media_hdl_ops = ...;
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

void rtp_sink_set_frame_by_buf(rtp_sink_t *sink)
{
	sink->sink_flag &= ~SINK_FLAG_FRAME_BY_REF;
	sink->sink_flag |= SINK_FLAG_FRAME_BY_BUF;
}

void rtp_sink_set_frame_by_none(rtp_sink_t *sink)
{
        sink->sink_flag &= ~(SINK_FLAG_FRAME_BY_REF | SINK_FLAG_FRAME_BY_BUF);
}

int rtp_sink_get_frame(rtp_sink_t *sink, int index, u8 *src, int len)
{
	struct rtp_packet *pckt = sink->packet;
        //hook data by reference of external buffer
        pckt->index = index;
        pckt->data = src;
        pckt->len = len;
        rtp_sink_ind_frame_ready(sink);
	return 0;
}

int rtp_sink_update_ts(rtp_sink_t *sink, u32 ts)
{
	sink->now_ts = ts;
	return 0;
}

void rtp_sink_packet_free(rtp_sink_t *sink)
{
        struct rtp_packet *pckt = sink->packet;
        if(pckt != NULL)
        {
          rtw_mutex_free(&pckt->lock);
          free(pckt);
          pckt = NULL;
        }
}

int rtp_sink_packet_create(rtp_sink_t *sink)
{
	struct rtp_packet *pckt = malloc(sizeof(struct rtp_packet));
	if(pckt == NULL)
	{
		RTP_ERROR("allocate sink packet failed");
		return -1;
	}
        memset(pckt, 0, sizeof(struct rtp_packet));
        rtw_mutex_init(&pckt->lock);
	sink->packet = pckt;
	return 0;
}

int rtp_sink_packet_init(rtp_sink_t *sink)
{
        
        //do we need to reset all members for safety?
        return 0;
}

//signal that frame is sent
void rtp_sink_ind_frame_sent(rtp_sink_t *sink)
{
        struct rtp_packet *pckt = sink->packet;
        rtw_mutex_get(&pckt->lock);
        pckt->status = RTP_PCKT_IDLE;
        rtw_mutex_put(&pckt->lock);
        //printf("\n\rsent");
}

void rtp_sink_ind_frame_ready(rtp_sink_t *sink)
{
        struct rtp_packet *pckt = sink->packet;
        rtw_mutex_get(&pckt->lock);
        pckt->status = RTP_PCKT_READY;
        rtw_mutex_put(&pckt->lock);        
        //printf("\n\rready");
}     

void rtp_sink_ind_frame_process(rtp_sink_t *sink)
{
        struct rtp_packet *pckt = sink->packet;
        rtw_mutex_get(&pckt->lock);
        pckt->status = RTP_PCKT_PROCESS;
        rtw_mutex_put(&pckt->lock);        
        //printf("\n\rprocess");
}

//return 0 means frame has been sent
int rtp_sink_wait_frame_sent(rtp_sink_t *sink)
{
        struct rtp_packet *pckt = sink->packet;  
        if(pckt->status == RTP_PCKT_IDLE)
            return 0;
	else
            return -1;
}
                        
int rtp_sink_wait_frame_ready(rtp_sink_t *sink)
{
        struct rtp_packet *pckt = sink->packet; 
        //printf("\n\r%d", pckt->status);
        if(pckt->status == RTP_PCKT_READY)
            return 0;
	else
            return -1;
}  

int rtp_sink_stats_init(rtp_sink_t *sink)
{
	return 0;
}

int rtp_sink_rtcp_init(rtp_sink_t *sink)
{
	return 0;
}
