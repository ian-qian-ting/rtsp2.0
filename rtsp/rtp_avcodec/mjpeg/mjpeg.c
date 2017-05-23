#include "FreeRTOS.h"
#include <platform/platform_stdlib.h>
#include "platform_opts.h"

#include "rtp_sink.h"
#include "rtp_source.h"
#include "rtsp_server.h"
#include "rtp_avcodec/mjpeg/mjpeg.h"
#include "sockets.h"
#include "lwip/netif.h"

#define WRITE_SIZE 1450   
extern int max_skb_buf_num;               
extern int skbdata_used_num;


static void parse_jpeg_header(u8 *jpeg_data, int len, int *width, int *height, u8 *type, u16 *dri, u8 *precision, u8 *lqt, u8 *cqt, int *hdr_len){
	u8 *ptr;
        u8 *start;
        if(jpeg_data == NULL)
        {
            printf("\n\rnull jpeg data!\n\r");
            return;
        }
	ptr = start = jpeg_data;
        u8 i;
        u16 rec; //header length record

        while((ptr - start) < len)
        {
          if(*ptr == 0xff)
          {
              switch(*(ptr + 1))
              {              
                case(0xdb):     //parse quantization table
                  ptr += 4;
                  *precision = (*ptr)>>4;
                  i = (*ptr)&(0x0f);
                  ptr ++;
                  if(*precision != 0) //16 bit precision
                  {
                      if(i == 0)
                          memcpy(lqt, ptr, 128);
                      else
                          memcpy(cqt, ptr, 128);
                      ptr +=128;
                  }else{              //8 bit precision  
                      if(i == 0)
                          memcpy(lqt, ptr, 64);
                      else
                          memcpy(cqt, ptr, 64);
                      ptr +=64;
                  }
                  break;                
                case(0xc4):     //parse DHT - skip here
                  ptr += 2;
                  rec = (*ptr << 8) | *(ptr + 1);
                  ptr += rec;
                  break;                
                case(0xc0):     //parse SOF0
                  ptr += 5;
                  *height = *ptr << 8 | *(ptr + 1);
                  ptr += 2;
                  *width = *ptr << 8 | *(ptr + 1);
                  ptr += 2;
                  if(*ptr == 3) //parse component
                  {
                      ptr++;
                      if(*(ptr+1) == 0x21)
                        *type = 0;
                      else if(*(ptr+1) == 0x22)
                        *type = 1;
                      ptr += 9;
                  }
                  break;
                case(0xc1):     //parse SOF1 - skip here
                  ptr++;
                  break;
                case(0xdd):     //parse dri
                  ptr += 4;
                  *dri = *ptr << 8 | *(ptr + 1);
                  ptr += 2;
                  break;
                /*end of parsing condition*/
                case(0xda):     //parse SOS and return
                  ptr += 2;
                  rec = (*ptr << 8) | *(ptr + 1);
                  ptr += rec;
                  *hdr_len = ptr - start;
                case(0xd9):
                  return;
                default:
                  ptr++;
              }
          }else{
              ptr++;
          }
        }
}

static void fillJpegHeader(struct jpeghdr *jpghdr, u8 type, u8 typespec, int width, int height, u16 dri, u8 q)
{
        jpghdr->tspec = typespec;
        jpghdr->type = type | ((dri != 0) ? RTP_JPEG_RESTART : 0);
        jpghdr->q = q;
        jpghdr->width = (u8)(width / 8);
        jpghdr->height = (u8)(height / 8);
}

static void fillRstHeader(struct jpeghdr_rst *rsthdr, u16 dri)
{
        rsthdr->dri = htons(dri);
	if (dri != 0) {
            rsthdr->f = 1;        /* This code does not align RIs */
            rsthdr->l = 1;
            rsthdr->count = 0x3fff;
        }
}

static void fillqtable(struct jpeghdr_qtable *qtable, u8 precision)
{
        qtable->mbz = 0;
        qtable->precision = precision; 
        if(precision != 0)
        {
            qtable->length = htons(256); // 2*128 quantization table length in network byte order
        }else{
            qtable->length = htons(128); // 2*64 quantization table length in network byte order
        }          
}

int mjpeg_hdl_extra_init(void *ctx)
{
        rtsp_sm_subsession *subsession = (rtsp_sm_subsession *)ctx;
        rtp_sink_t *sink = subsession->sink;
        struct rtp_packet *pckt = sink->packet;

        struct rtp_jpeg_obj *jpeg_obj = malloc(sizeof(struct rtp_jpeg_obj));
        if(jpeg_obj == NULL)
        {
            MJPEG_ERROR("allocate rtp jpeg object failed");
            return -1;
        }
        memset(jpeg_obj, 0, sizeof(struct rtp_jpeg_obj));
        pckt->extra = (void *)jpeg_obj;
        return 0;
}

void mjpeg_hdl_extra_deinit(void *ctx)
{
        rtsp_sm_subsession *subsession = (rtsp_sm_subsession *)ctx;
        rtp_sink_t *sink = subsession->sink;
        struct rtp_packet *pckt = sink->packet; 
        if(pckt->extra != NULL)
            free(pckt->extra);
        pckt->extra = NULL;
}

int mjpeg_hdl_send(void *ctx)
{
	rtsp_sm_subsession *subsession = (rtsp_sm_subsession *)ctx;
        rtp_sink_t *sink = subsession->sink;
        struct rtp_packet *pckt = sink->packet;
        struct rtp_jpeg_obj *jpeg_obj = (struct rtp_jpeg_obj *)pckt->extra;
        rtp_hdr_t *rtphdr;
        struct jpeghdr *jpghdr;
        int i, ret;
        u8 type, precision;
        u16 dri;
        int rtp_width, rtp_height;
        u8 buf[WRITE_SIZE];
        u8 *ptr, *tmp, *data_entry;
        int bytes_left, retry_cnt;
        int header_len, dqt_len, data_len, offset;
        
        int socket = sink->rtp_sock;
        struct sockaddr_in adr_cs;
        int len_cs = sizeof(adr_cs);
        adr_cs.sin_family = AF_INET;
        adr_cs.sin_addr.s_addr = *(uint32_t *)subsession->client.client_ip;
        adr_cs.sin_port = htons(subsession->client.transport.client_port_even);
        
        type = precision = dri = 0;
        rtp_width = rtp_height = 0;
        header_len = dqt_len = data_len = offset = 0;

        parse_jpeg_header(pckt->data, pckt->len, &rtp_width, &rtp_height, &type, &dri, &precision, jpeg_obj->lqt, jpeg_obj->cqt, &jpeg_obj->hdr_len);
        data_entry = pckt->data;

        rtp_fill_header(&pckt->rtphdr, 2, 0, 0, 0, 0, sink->pt, sink->seq_no, sink->now_ts, sink->ssrc);
        fillJpegHeader(&jpeg_obj->jpghdr, type, /*typespec*/0, rtp_width, rtp_height, dri, /*q*/USE_EXPLICIT_DQT);
        fillRstHeader(&jpeg_obj->rsthdr, dri);
        fillqtable(&jpeg_obj->qtable, precision);
        //dumpJpegHeader(&jpeg_obj->jpghdr);
        //send packet
        bytes_left = pckt->len;
        ptr = buf;
        //ignore rtp header cc check since we only allow single source
        memcpy(ptr, &pckt->rtphdr, RTP_HDR_SZ);
        rtphdr = (rtp_hdr_t *)ptr;
        ptr += RTP_HDR_SZ;
        
        memcpy(ptr, &jpeg_obj->jpghdr, sizeof(jpeg_obj->jpghdr));
        jpghdr = (struct jpeghdr *)ptr;
        ptr += sizeof(jpeg_obj->jpghdr);
        
        if(jpeg_obj->frame_offset == 0)
            jpghdr->off = 0;
        if(jpeg_obj->rsthdr.dri > 0)
        {
            memcpy(ptr, &jpeg_obj->rsthdr, sizeof(jpeg_obj->rsthdr));
            ptr += sizeof(jpeg_obj->rsthdr);
        }else{
            //to fix logitech c160 no dri bug
            jpghdr->q = 0;
        }
        header_len = ptr - buf;
        //printf("entering frame sending loop\n\r");
        while(bytes_left > 0){
            if(offset == 0)
            {
                if(jpghdr->q >= 128)
                {
                    tmp = ptr;
                    memcpy(tmp, &jpeg_obj->qtable, sizeof(jpeg_obj->qtable));
                    tmp += sizeof(jpeg_obj->qtable);
                    if(jpeg_obj->qtable.precision != 0)
                    {
                        memcpy(tmp, jpeg_obj->lqt, 128);
                        tmp += 128;
                        memcpy(tmp, jpeg_obj->cqt, 128);
                        tmp += 128;
                    }else{
                        memcpy(tmp, jpeg_obj->lqt, 64);
                        tmp += 64;
                        memcpy(tmp, jpeg_obj->cqt, 64);
                        tmp += 64;                    
                    }
                    dqt_len = tmp - ptr;
                    header_len += (tmp - ptr);
                    data_entry += jpeg_obj->hdr_len;
                    bytes_left -= jpeg_obj->hdr_len;
                }else{
                    
                    dqt_len = 0;
                }
            }else{
                header_len = ptr - buf;
                dqt_len = 0;
            }
            data_len = WRITE_SIZE - header_len;
            if(data_len >= bytes_left)
            {
                data_len = bytes_left;
                rtphdr->m = 1;
            }
            memcpy(ptr + dqt_len, data_entry + offset, data_len);
check_skb:
            if(skbdata_used_num > (max_skb_buf_num - 3)){
                rtw_msleep_os(1);
                goto check_skb;
            }else{
                retry_cnt = 3;
                if(sendto(socket, buf, header_len + data_len, 0, (struct sockaddr *)&adr_cs, len_cs) < 0)
                {
                  do{
                      rtw_msleep_os(1);
                      sendto(socket, buf, header_len + data_len, 0, (struct sockaddr *)&adr_cs, len_cs);
                      retry_cnt--;
                  }while(ret < 0 && retry_cnt > 0);
                  if(ret < 0)
                      return -EAGAIN;
                }
            }
            offset += data_len;
            jpeg_obj->frame_offset += data_len;
            jpghdr->off = ((jpeg_obj->frame_offset & 0xff) << 16 | (jpeg_obj->frame_offset & 0xff00) | (jpeg_obj->frame_offset & 0xff0000UL) >> 16);
            bytes_left -= data_len;
            sink->seq_no++; 
            rtphdr->seq = htons(sink->seq_no);
            sink->octet_cnt += (header_len + data_len);
        }
        sink->packet_cnt++;
        
        return 0;
}

int mjpeg_hdl_recv(void *ctx)
{
    return 0;
}

struct avcodec_handle_ops mjpeg_hdl_ops =
{
        .packet_extra_init = mjpeg_hdl_extra_init,
        .packet_extra_deinit = mjpeg_hdl_extra_deinit,
	.packet_send = mjpeg_hdl_send,
	.packet_recv = mjpeg_hdl_recv
};

#if MJPEG_DEBUG
void dumpJpegHeader(struct jpeghdr *jpghdr)
{
        printf("\n\rJpeg header info:");
        printf("\n\rid of jpeg decoder params:%d", jpghdr->type);
        printf("\n\rquantization factor (or table id):%d", jpghdr->q);
        printf("\n\rframe width in 8 pixel blocks:%d", jpghdr->width);
        printf("\n\rframe height in 8 pixel blocks:%d", jpghdr->height);
}

void dumpRstDeader(struct jpeghdr_rst *rsthdr)
{
        printf("\n\rRestart header info:");
        printf("\n\rRestart interval:%d", ntohs(rsthdr->dri));
        printf("\n\rRestart first bit set:%d", rsthdr->f);
        printf("\n\rRestart last bit set:%d", rsthdr->l);
        printf("\n\rRestart Count:%d", rsthdr->count);
}
#else
void dumpJpegHeader(struct jpeghdr *jpghdr)
{
}

void dumpRstDeader(struct jpeghdr_rst *rsthdr)
{
}
#endif