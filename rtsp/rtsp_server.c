#include "FreeRTOS.h"
#include "task.h"
#include "platform/platform_stdlib.h"
#include "osdep_service.h"
#include "rtsp_rtp_dbg.h"
#include "rtsp_common.h"
#include "rtsp_server.h"

#include "sockets.h" //for sockets
#include "wifi_conf.h"
#include "wifi_util.h" //for getting wifi mode info
#include "lwip/netif.h" //for LwIP_GetIP

#define RTSP_IP_SIZE	4

#define RTSP_SERVICE_PRIORITY   2
#define RTP_SERVICE_PRIORITY    (RTSP_SERVICE_PRIORITY - 1)

extern struct netif xnetif[NET_IF_NUM];
extern uint8_t* LwIP_GetIP(struct netif *pnetif);

static u32 rtsp_launch_timeout = 60000; //in ms

static u8 lower_port_bitmap = 0;
static _mutex lower_port_lock = NULL;
static u8 client_lower_port_bitmap = 0;
static _mutex client_lower_port_lock = NULL;
static u8 server_lower_port_bitmap = 0;
static _mutex server_lower_port_lock = NULL;
static atomic_t lock_ref_cnt = {0};

//for debug purpose only
#if 1
void rtsp_req_dump(u8 *str, int len)
{
    str[len] = '\0';
    printf("\n\rrequest:");
    printf("\n\r%s", str);
}

void rtsp_res_dump(u8 *str, int len)
{
    str[len] = '\0';
    printf("\n\rresponse:");
    printf("\n\r%s", str);
}

void rtsp_transport_dump(struct rtsp_transport *transport)
{
  printf("\n\rcast_mode:%d", transport->cast_mode);
  printf("\n\rprotocol:%d", transport->proto);
  printf("\n\rlower protocol:%d", transport->lower_proto);
  printf("\n\rttl:%d", transport->ttl);
  printf("\n\rport_even:%d", transport->port_even);
  printf("\n\rclient_port_even:%d", transport->client_port_even);
  printf("\n\rserver_port_even:%d", transport->server_port_even);
  printf("\n\rssrc:%x", transport->ssrc);
}
#endif

//this range param should be aligned with port bitmap type size
static int rtsp_get_port(int base, int range, u8* bitmap)
{
	int find = -1;
        int i, tmp;
        tmp = *bitmap;
	for(i = 0; i < range; i += 2)
	{
		if(!((tmp>>i)&1))
		{
			//set bit
			find = base + i;
			*bitmap |= (1 << i);
			break;
		}
	}
	return find;
}

static void rtsp_put_port(int base, int range, u8* bitmap, int port)
{
        int tmp = *bitmap;
	int bit = (port - base)/2;
	if((tmp>>bit)&1)
	{
		//clear bit
		*bitmap &= !(1 << bit);
	}
}

static u8* rtsp_parse_header_line(struct rtsp_message *msg, u8 *header, int *size_left)
{
		u8 *p_start, *p_tmp, *p_end;
		u8 offset = 0;
		int parse_item = 0;
		u8 b_tmp[16];
		p_start = p_tmp = p_end = header;

		while(!is_line_end(p_end) && offset <= *size_left)
		{
			if(*p_end == ' ' || *p_end == '\r')
			{
				memset(b_tmp, 0, 16);
				if(parse_item == 0)//parse method
				{
					parse_item++;
					memcpy(b_tmp, p_tmp, p_end-p_tmp);
					b_tmp[p_end-p_tmp] = '\0';
					if(!strcmp(b_tmp, "OPTIONS"))
					{
						msg->method = RTSP_REQ_OPTIONS;
					}else if(!strcmp(b_tmp, "DESCRIBE"))
						{
							msg->method = RTSP_REQ_DESCRIBE;
						}else if(!strcmp(b_tmp, "SETUP"))
							{
								msg->method = RTSP_REQ_SETUP;
							}else if(!strcmp(b_tmp, "TEARDOWN"))
								{
									msg->method = RTSP_REQ_TEARDOWN;
								}else if(!strcmp(b_tmp, "PLAY"))
									{
										msg->method = RTSP_REQ_PLAY;
									}else if(!strcmp(b_tmp, "PAUSE"))
										{
											msg->method = RTSP_REQ_PAUSE;
										}else if(!strcmp(b_tmp, "GET_PARAMETER"))
											{
												msg->method = RTSP_REQ_GET_PARAMETER;
											}else{
													msg->method = RTSP_REQ_UNDEFINED;
												}				
				}else if(parse_item == 1)//parse request uri
					{
						parse_item++;
						//to do
					}else if(parse_item == 2)//parse rtsp version
						{
							parse_item++;
							//ignore
						}
				p_tmp = p_end + 1;//move to next item
			}
			p_end++;
			offset++;
		}
		if(*p_end == '\n')
		{
			//copy request line without CRLF
			if(p_end-p_start-1 < REQ_LINE_BUF_SIZE)
			{
				memcpy(msg->request_line, p_start, p_end-p_start-1);
				msg->request_line[p_end-p_start] = '\0';
			}
			else
			{
				memcpy(msg->request_line, p_start, REQ_LINE_BUF_SIZE - 1);
				msg->request_line[REQ_LINE_BUF_SIZE - 1] = '\0';
			}
			//skip CRLF
			p_end += 1;
			offset += 1;
			*size_left -= offset;
			return p_end;
		}else{
			//fall into error
			*size_left = 0;
			return NULL;
			}
}

static u8* rtsp_parse_body_line(struct rtsp_message *msg, u8 *body, int *size_left)
{
		u8 *p_start, *p_tmp, *p_end, *token;
		u8 offset = 0;
		int parse_item = 0;
		char delimeter [] = "/=-";
		u8 b_tmp[64] = {0};
		p_start = p_tmp = p_end = body;
		while(!is_line_end(p_end) && offset <= *size_left)
		{
			//parse header field type
			if(*p_end == ':')
			{
				//sanity check
				if(*(p_end + 1) == ' ')
				{
					memcpy(b_tmp, p_tmp, p_end - p_tmp);
					b_tmp[p_end - p_tmp] = '\0';
					p_end = p_end + 2; //skip colon ':' and space ' '
					offset += 2;
					p_tmp = p_end;
					break;
				}
			}	
			p_end++;
			offset++;
		}
		
		if(!strncmp(b_tmp, "CSeq", 4))
		{
			memset(b_tmp, 0, 64);
			while(!is_line_end(p_end) && offset <= *size_left)
			{
				if(*p_end == '\r')
				{
					memcpy(b_tmp, p_tmp, p_end - p_tmp);
					b_tmp[p_end - p_tmp] = '\0';
					msg->CSeq = atoi(b_tmp);
					break;
				}
				p_end++;
				offset++;
			}
		}else if(!strncmp(b_tmp, "Transport", 9))
			{
				while(!is_line_end(p_end) && offset <= *size_left)
				{
					if(*p_end == ';'||*p_end == '\r')
					{	
						if(parse_item == 0)
						{
						//parse transport specifier <transport/profile/lower-transport>, default is RTP/AVP/XXX
							parse_item++;
							memset(b_tmp, 0, 64);
							memcpy(b_tmp, p_tmp, p_end - p_tmp);
							b_tmp[p_end - p_tmp] = '\0';
							token = strtok(b_tmp, delimeter);//get RTP
							if(!strncmp(token, "RTP", 3))
								msg->transport.proto = TRANS_PROTO_RTP;
							else
								msg->transport.proto = TRANS_PROTO_UNKNOWN;
							token = strtok(NULL, delimeter);//get AVP
							token = strtok(NULL, delimeter);//get lower-transport
							if(token == NULL || !strncmp(token, "UDP", 3))
							{
								msg->transport.lower_proto = TRANS_LOWER_PROTO_UDP;
							}else if(!strncmp(token, "TCP", 3)){
								msg->transport.lower_proto = TRANS_LOWER_PROTO_TCP;
							}else
							{
								msg->transport.lower_proto = TRANS_LOWER_PROTO_UNKNOWN;
							}
						}else{
							memset(b_tmp, 0, 64);
							memcpy(b_tmp, p_tmp, p_end - p_tmp);
							b_tmp[p_end - p_tmp] = '\0';
							if(!strncmp(b_tmp, "unicast", 7))
								msg->transport.cast_mode = UNICAST_MODE;
							else if(!strncmp(b_tmp, "multicast", 9))
								msg->transport.cast_mode = MULTICAST_MODE;
/*							
							else if(!strncmp(b_tmp, "destination", 11))
								//to do
							else if(!strncmp(b_tmp, "interleaved", 11))
								//to do
							else if(!strncmp(b_tmp, "append", 6))
								//to do
							else if(!strncmp(b_tmp, "ttl", 3))
								//to do
							else if(!strncmp(b_tmp, "port", 4))
								//to do
*/
							else if(!strncmp(b_tmp, "client_port", 11))
							{
								memset(b_tmp, 0, 64);
								memcpy(b_tmp, p_tmp, p_end - p_tmp);
								b_tmp[p_end - p_tmp] = '\0';	
								token = strtok(b_tmp, delimeter); //get "client_port"
								token = strtok(NULL, delimeter);
								if(token != NULL)
									msg->transport.client_port_even = atoi(token);
								token = strtok(NULL, delimeter);
								if(token != NULL)
									msg->transport.client_port_odd = atoi(token);
							}
							else if(!strncmp(b_tmp, "port", 4))
							{
								memset(b_tmp, 0, 64);
								memcpy(b_tmp, p_tmp, p_end - p_tmp);
								b_tmp[p_end - p_tmp] = '\0';	
								token = strtok(b_tmp, delimeter); //get "client_port"
								token = strtok(NULL, delimeter);
								if(token != NULL)
									msg->transport.port_even = atoi(token);
								token = strtok(NULL, delimeter);
								if(token != NULL)
									msg->transport.port_odd = atoi(token);								
							}
							else if(!strncmp(b_tmp, "server_port", 11))
							{
								memset(b_tmp, 0, 64);
								memcpy(b_tmp, p_tmp, p_end - p_tmp);
								b_tmp[p_end - p_tmp] = '\0';	
								token = strtok(b_tmp, delimeter); //get "server_port"
								token = strtok(NULL, delimeter);
								if(token != NULL)
									msg->transport.server_port_even = atoi(token);
								token = strtok(NULL, delimeter);
								if(token != NULL)
									msg->transport.server_port_odd = atoi(token);								
							}
							else if(!strncmp(b_tmp, "ssrc", 4))
							{
								memset(b_tmp, 0, 64);
								memcpy(b_tmp, p_tmp, p_end - p_tmp);
								b_tmp[p_end - p_tmp] = '\0';
								token = strtok(b_tmp, delimeter); //get "ssrc"
								token = strtok(NULL, delimeter);
								if(token != NULL)
									msg->transport.ssrc = atoi(token);
							}
/*  valid value are PLAY and RECORD but we don't support RECORD							
							else if(!strncmp(b_tmp, "mode", 4))
								//to do
*/							
						}
						
						if(*p_end == ';')
						{
							p_end++;
							offset++;
							p_tmp = p_end;//skip ';'
						}else if(*p_end == '\r')
						{
							break;
						}
					}else{
						p_end++;
						offset++;
					}
				}
			}
                while(!is_line_end(p_end) && offset <= *size_left)
                {
                        p_end ++;
                        offset ++;
                }

		if(*p_end == '\n')
		{
			//skip CRLF
			p_end += 1;
			offset += 1;
			*size_left -= offset;
			return p_end;
		}else{
                        printf("\n\rparsing line error");
			*size_left = 0;
			return NULL;
		}
}

int rtsp_parse_request(struct rtsp_message *msg, u8 *request, int size)
{
                u8 *p_start, *p_body;
		p_start = p_body = NULL;
		int size_left = size;
                msg->method = RTSP_REQ_UNDEFINED;
		//is it a rtsp request?
		if(request == NULL || *request == '\0' || size <= 0)
		{
			//RTSP_WARN("\n\rinvalid request - (NULL request)");
			return -EAGAIN;
		}
		p_body = rtsp_parse_header_line(msg, request, &size_left);
		if(msg->method == RTSP_REQ_UNDEFINED)
		{
			RTSP_WARN("\n\rinvalid request - (UNDEFINED request)");
			return -EINVAL;
		}			
		while(p_body != NULL && *p_body != '\0' && size_left > 0)
		{
                        //printf("\n\rline:%c size left:%d", *p_body, size_left);
			p_body = rtsp_parse_body_line(msg, p_body, &size_left);
		}
		return 0;
}

/* rtsp server media session */
void rtsp_set_rtp_task(rtsp_sm_subsession *subsession, void (*rtp_task_handle)(void *ctx));

void rtsp_sm_subsession_free(rtsp_sm_subsession *subsession)
{
		if(subsession->my_sdp != NULL)
			free(subsession->my_sdp);
		if(subsession != NULL)
			free(subsession);
}

void rtsp_sm_session_free(rtsp_sm_session *session)
{
		rtsp_sm_subsession *subsession = NULL;
		while(!list_empty(&session->media_entry))
		{
			subsession = list_first_entry(&session->media_entry, rtsp_sm_subsession, media_anchor);
			list_del(&subsession->media_anchor);
			rtsp_sm_subsession_free(subsession);
		}	
}

void rtsp_sm_subsession_refresh(rtsp_sm_subsession *subsession)
{
        rtsp_cc_session *c = &subsession->client;
        struct rtsp_transport *transport = &c->transport;
        if(c->is_handled)
        {
                c->client_socket = -1;
                //client ip is inherited from rtsp server struct 
                //so we dont need to free it here since it will be handled elsewhere
                c->client_ip = NULL;
                if(transport->client_port_even != 0)
                {
                        rtw_mutex_get(&client_lower_port_lock);
                        rtsp_put_port(CLIENT_LOWER_PORT_BASE, 8, &client_lower_port_bitmap, transport->client_port_even);
                        rtw_mutex_put(&client_lower_port_lock);                    
                }
                if(transport->server_port_even != 0)
                {
                        rtw_mutex_get(&server_lower_port_lock);
                        rtsp_put_port(SERVER_LOWER_PORT_BASE, 8, &server_lower_port_bitmap, transport->server_port_even);
                        rtw_mutex_put(&server_lower_port_lock);                 
                }      
                if(transport->port_even)
                {
                        rtw_mutex_get(&lower_port_lock);
                        rtsp_put_port(LOWER_PORT_BASE, 8, &lower_port_bitmap, transport->port_even);
                        rtw_mutex_put(&lower_port_lock);                 
                }
                memset(transport, 0, sizeof(struct rtsp_transport));
                c->is_handled = 0;
        }
        subsession->task_id = NULL;
        rtsp_set_rtp_task(subsession, NULL);
}

void rtsp_sm_session_refresh(rtsp_sm_session *session)
{
        struct rtsp_server *server = (struct rtsp_server *)session->parent_server;
        struct rtsp_session_info *s = &session->session_info;
        rtsp_sm_subsession *subsession = NULL;
	if(s->user != NULL)
		free(s->user);
	if(s->name != NULL)
		free(s->name);
	if(s->info != NULL)
		free(s->info); 
        memset(s, 0, sizeof(struct rtsp_session_info));
	list_for_each_entry(subsession, &server->server_media.media_entry, media_anchor, rtsp_sm_subsession)
	{        
                rtsp_sm_subsession_refresh(subsession);
        }
}

rtsp_sm_subsession * rtsp_sm_subsession_create(rtp_source_t *src, rtp_sink_t *sink, int max_sdp_size)
{
		rtsp_sm_subsession *subsession = malloc(sizeof(rtsp_sm_subsession));
		if(subsession == NULL)
		{
			RTSP_ERROR("\n\rsubsession allocate failed");
			return NULL;
		}
		memset(subsession, 0, sizeof(rtsp_sm_subsession));
		if((subsession->my_sdp = malloc(max_sdp_size)) == NULL)
		{
			RTSP_ERROR("\n\rcreate media sdp buffer failed");
			free(subsession);
			return NULL;
		}
		subsession->my_sdp_max_len = max_sdp_size;
		subsession->my_sdp_content_len = 0;
		INIT_LIST_HEAD(&subsession->media_anchor);
		if(sink != NULL)
			subsession->sink = sink;
		if(src != NULL)
			subsession->src = src;
		return subsession;
}

int rtsp_sm_subsession_add(rtsp_sm_session *session, rtsp_sm_subsession *subsession)
{
		//p_rtsp_sm_subsession tmp = NULL;
		//check if server reach maximum subsession number
		if(ATOMIC_READ(&session->subsession_cnt) >= session->max_subsession_nb)
		{
			RTSP_WARN("\n\rmax subsession cnt reached!");
			return -EPERM;
		}
		subsession->id = ATOMIC_READ(&session->subsession_cnt);
		list_add_tail(&subsession->media_anchor, &session->media_entry);
		subsession->parent_session = (void *)session;
                ATOMIC_INC(&session->subsession_cnt);
		return 0;
}

void rtsp_sm_clear_session(rtsp_sm_session *session)
{
		INIT_LIST_HEAD(&session->media_entry);
		session->my_sdp_content_len = 0;
		ATOMIC_SET(&session->subsession_cnt, 0);
		ATOMIC_SET(&session->reference_cnt, 0);			
}

void rtsp_sm_clear_all(rtsp_sm_session *session)
{
		if(session->my_sdp != NULL)
			free(session->my_sdp);
		session->parent_server = NULL;
		INIT_LIST_HEAD(&session->media_entry);
		session->my_sdp_max_len = 0;
		session->my_sdp_content_len = 0;
		ATOMIC_SET(&session->subsession_cnt, 0);
		ATOMIC_SET(&session->reference_cnt, 0);		
}

int rtsp_sm_setup(rtsp_sm_session *session, void *parent, int max_subsession_nb, int max_sdp_size)
{
		if(session->my_sdp)
			free(session->my_sdp);
		session->my_sdp = NULL;
		if((session->my_sdp = malloc(max_sdp_size)) == NULL)
		{
			RTSP_ERROR("\n\rcreate media sdp buffer failed");
			return -ENOMEM;
		}
		session->parent_server = parent;
		session->max_subsession_nb = max_subsession_nb;
		session->my_sdp_max_len = max_sdp_size;
		INIT_LIST_HEAD(&session->media_entry);
		session->my_sdp_content_len = 0;
		ATOMIC_SET(&session->subsession_cnt, 0);
		ATOMIC_SET(&session->reference_cnt, 0);
		return 0;
}

/* end of rtsp server media session */

void rtsp_server_free(struct rtsp_server *server)
{
		rtsp_sm_session_free(&server->server_media);
		free(server->adapter);
		free(server->client_ip);
		free(server->server_ip);
		free(server);
                if(ATOMIC_DEC_AND_TEST(&lock_ref_cnt))
                {
                    rtw_mutex_free(&client_lower_port_lock);
                    rtw_mutex_free(&server_lower_port_lock);
                    rtw_mutex_free(&lower_port_lock);
                }
}

struct rtsp_server *rtsp_server_create(rtsp_server_adapter *adapter)
{
		struct rtsp_server *server = malloc(sizeof(struct rtsp_server));
		if(server == NULL)
		{
				RTSP_ERROR("\n\rallocate server failed");
				return NULL;
		}
		memset(server, 0, sizeof(*server));
		//default server ipv4
		if((server->server_ip = malloc(RTSP_IP_SIZE)) == NULL)
		{
				RTSP_ERROR("\n\rallocate server ip failed");
				free(server);
				return NULL;		
		}
		if((server->client_ip = malloc(RTSP_IP_SIZE)) == NULL)
		{
				RTSP_ERROR("\n\rallocate client ip failed");
				free(server->server_ip);
				free(server);
		}
		//default server media setup
		if(rtsp_sm_setup(&server->server_media, (void *)server, \
		(adapter->max_subsession_nb <= 0) ? 1 : adapter->max_subsession_nb, MAX_SDP_SIZE) < 0)
		{
			RTSP_ERROR("\n\rmedia setup failed");
			free(server->client_ip);
			free(server->server_ip);
			free(server);
			return NULL;
		}
		server->server_socket = -1;
                server->client_socket = -1;
		server->adapter = adapter;
                if(client_lower_port_lock == NULL)
                    rtw_mutex_init(&client_lower_port_lock);
                if(server_lower_port_lock == NULL)
                    rtw_mutex_init(&server_lower_port_lock);
                if(lower_port_lock == NULL)
                    rtw_mutex_init(&lower_port_lock);
                ATOMIC_INC(&lock_ref_cnt);
		return server;
}

int rtsp_server_setup(struct rtsp_server *server, const u8* server_url, int port)
{
		if(!server)
		{
			RTSP_WARN("\n\rserver invalid");
			return -EINVAL;
		}
		if(server->is_launched)
		{
			RTSP_WARN("\n\rserver launched (permission denied)");
			return -EPERM;
		}
		if(list_empty(&server->server_media.media_entry))
		{
			RTSP_WARN("\n\rmedia not ready (permission denied)");
			return -EPERM;
		}
		if(server->is_setup)
		{
			//do clean setup resource if any
			server->is_setup = 0;
		}
		if(server_url != NULL && server_url[0] != '\0')
		{
			memset(server->server_url, 0, MAX_URL_LEN);
			memcpy(server->server_url, server_url, (strlen(server_url)<(MAX_URL_LEN - 1))? strlen(server_url):(MAX_URL_LEN - 1));
		}else{
			//add default url here?
		}
		//store server port to be used
		if(port > 0)
			server->server_port = port;
		else
			server->server_port = RTSP_PORT_DEF;
		//
		server->is_setup = 1;
		return 0;
}

void rtp_unicast_service(void *ctx)
{
        int ret;
	p_rtsp_sm_subsession subsession = (p_rtsp_sm_subsession)ctx;
        rtp_sink_t *sink = subsession->sink;
	p_rtsp_sm_session session = subsession->parent_session;
	struct rtsp_server *server = (struct rtsp_server *)session->parent_server;
	int rtp_socket, rtp_port;
	struct sockaddr_in rtp_addr;
	socklen_t rtp_addrlen = sizeof(struct sockaddr_in);
#if 0
	int rtcp_socket, rtcp_port;
	struct sockaddr_in rtcp_addr;
	socklen_t rtcp_addrlen = sizeof(struct sockaddr_in);
#endif	
        //wait until server state change to RTSP_PLAYING
        //rtw_msleep_os(1000);
        
	rtp_socket = socket(AF_INET, SOCK_DGRAM, 0);
	rtp_port = subsession->client.transport.server_port_even;
	memset(&rtp_addr, 0, rtp_addrlen);
	rtp_addr.sin_family = AF_INET;
	rtp_addr.sin_addr.s_addr = *(uint32_t *)(server->server_ip);
        rtp_addr.sin_port = _htons(rtp_port);
	if(bind(rtp_socket, (struct sockaddr *)&rtp_addr, rtp_addrlen)<0)
	{
		RTSP_ERROR("bind failed");
		goto exit;
	}
#if 0	
	rtcp_socket = socket(AF_INET, SOCK_DGRAM, 0);
	rtcp_port = subsession->client.transport.server_port_odd;
	memset(&rtp_addr, 0, rtcp_addrlen);
	rtcp_addr.sin_family = AF_INET;
	rtcp_addr.sin_addr.s_addr = *(uint32_t *)(server->server_ip);
        rtcp_addr.sin_port = _htons(rtcp_port);
	if(bind(rtcp_socket, (struct sockaddr *)&rtcp_addr, rtcp_addrlen)<0)
	{
		RTSP_ERROR("bind failed");
		goto exit;
	}
#endif
	//default implementation via UDP
	//init sink status here
        sink->rtp_sock = rtp_socket;
#if 0
        sink->rtcp_sock = rtcp_socket;
#endif
        sink->ssrc = subsession->client.transport.ssrc;
        sink->base_ts = 0;
        sink->seq_no = 0;
        sink->packet_cnt = 0;
        sink->octet_cnt = 0;
        sink->total_octet_cnt = 0;
        
        //init codec specific extra ctx if any
        if(subsession->sink->media_hdl_ops->packet_extra_init)
        {
            ret = subsession->sink->media_hdl_ops->packet_extra_init((void *)subsession);
            if(ret < 0)
                goto exit;
        }
	//do we need a signal to indicate service start?
        ATOMIC_INC(&server->server_media.reference_cnt);
restart:	
	while(server->state_now == RTSP_PLAYING && server->is_launched)
	{
		if(subsession->sink->media_hdl_ops->packet_send)
                {
                    if(rtp_sink_wait_frame_ready(sink) < 0)
                          continue;
                    rtp_sink_ind_frame_process(sink);
                    ret = subsession->sink->media_hdl_ops->packet_send((void *)subsession);
                    if(ret < 0)
                    {
                        //record packet loss
                    }
                    rtp_sink_ind_frame_sent(subsession->sink);
                }
                    //update status here 
                if(sink->seq_no == 0)
                    sink->base_ts = sink->now_ts;
                //rtw_msleep_os(1);
	}
pause:
	rtw_msleep_os(1000);
	if(server->state_now == RTSP_READY)
	{
		goto restart;
	}
        ATOMIC_DEC(&server->server_media.reference_cnt);
        //deinit codec specific extra ctx if any
        if(subsession->sink->media_hdl_ops->packet_extra_deinit)
                subsession->sink->media_hdl_ops->packet_extra_deinit((void *)subsession);        
exit:
        server->state_now = RTSP_INIT;  
	close(rtp_socket);
#if 0
        close(rtcp_socket);
#endif        
        RTSP_INFO("rtp session closed");
	vTaskDelete(NULL);	
}

void rtp_multicast_service(void *ctx)
{
	
}

int rtsp_on_req_OPTIONS(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter))
{       
	u8 response[256] = {0};
	if(server->CSeq_now > server->message.CSeq && server->state_now != RTSP_INIT)
        {
                RTSP_WARN("CSeq out of order");
		return -EINVAL;
        }
	server->CSeq_now = server->message.CSeq;
	if(rtsp_req_cb)
            rtsp_req_cb(server->adapter->ext_adapter);
	sprintf(response, RTSP_RES_OK CRLF \
						"CSeq: %d" CRLF \
						PUBLIC_CMD_STR CRLF \
						CRLF, server->CSeq_now);
        //rtsp_res_dump(response, strlen(response));
	return write(server->client_socket, response, strlen(response));
}

static void rtsp_session_info_set(struct rtsp_session_info *s, u32 session_id, u32 session_timeout, u8 *user, u8 *name, u8 *info, u32 version, u64 start_time, u64 end_time)
{
	if(session_id > 0x10000000)
		s->session_id = session_id;
	else 
	{
		rtw_get_random_bytes((void *)&s->session_id, sizeof(s->session_id));
		if(s->session_id < 0x10000000)
			s->session_id += 0x10000000;
	}
	if(session_timeout >= 30000)
		s->session_timeout = session_timeout;
	else
		s->session_timeout = DEF_SESSION_TIMEOUT;
	if(user != NULL)
		s->user = user;
	else
		s->user = NULL;
	if(name != NULL)
		s->name = name;
	else
		s->name = NULL;
	if(info != NULL)
		s->info = info;
	else
		s->info = NULL;
	s->version = version;
	s->start_time = start_time;
	s->end_time = end_time;
}

#if 0
static u8 *data_to_hex(u8 *buff, u8 *src, int s, int lowercase)
{
    int i;
    static const char hex_table_uc[16] = { '0', '1', '2', '3',
                                        '4', '5', '6', '7',
                                        '8', '9', 'A', 'B',
                                        'C', 'D', 'E', 'F' };
    static const char hex_table_lc[16] = { '0', '1', '2', '3',
                                        '4', '5', '6', '7',
                                        '8', '9', 'a', 'b',
                                        'c', 'd', 'e', 'f' };
    const char *hex_table = lowercase ? hex_table_lc : hex_table_uc;
    for(i = 0; i < s; i++) {
        buff[i * 2]     = hex_table[src[i] >> 4];
        buff[i * 2 + 1] = hex_table[src[i] & 0xF];
    }

    return buff;
}

static u8 *extradata2config(void *extra)
{
   u8 *config;

   if(strlen(extra) > 1024)
   {
        RTSP_INFO("\n\rtoo much extra data!");
        return NULL;
   }
   config = malloc(10 + strlen(extra)*2);
   if (config == NULL) {
       RTSP_INFO("\n\rallocate config memory failed");
       return NULL;
   }
   memcpy(config, "; config=", 9);
   data_to_hex(config + 9, extra, strlen(extra), 1);
   config[9 + strlen(extra) * 2] = 0;

   return config;
}

static int get_frequency_index(int samplerate)
{
	uint32_t freq_idx_map[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
	for(int i=0;i<sizeof(freq_idx_map)/sizeof(freq_idx_map[0]);i++){
		if(samplerate==freq_idx_map[i])
			return i;
	}
	return 0xf;		// 15: frequency is written explictly 
}
#endif

static void sdp_fill_subsession_a_field(u8 *buf, int max_len, rtsp_sm_subsession *subsession)
{
	rtp_sink_t *sink = subsession->sink;
        unsigned char string[128];
	unsigned char spspps_str[128];
	//do we need to check if has sink?
	switch(sink->codec_id){
		case(AV_CODEC_ID_MJPEG):
			sprintf(string, "a=rtpmap:%d JPEG/%d" CRLF \
							"a=control:streamid=%d" CRLF \
							"a=framerate:%d" CRLF \
							, sink->pt, sink->frequency, subsession->id, sink->frame_rate);
			break;
		case(AV_CODEC_ID_H264):

			sprintf(string, "a=rtpmap:%d H264/%d" CRLF \
							"a=control:streamid=%d" CRLF \
							"a=fmtp:%d packetization-mode=0%s" CRLF \
							, sink->pt + subsession->id, sink->frequency, subsession->id, sink->pt + subsession->id, spspps_str);
			break;
		case(AV_CODEC_ID_PCMU):
			sprintf(string, "a=rtpmap:%d PCMU/%d" CRLF             \
							"a=ptime:20" CRLF						\
							"a=control:streamid=%d" CRLF            \
							, sink->pt, sink->frequency, subsession->id); 
			break;		
		case(AV_CODEC_ID_PCMA):
			sprintf(string, "a=rtpmap:%d PCMA/%d" CRLF             \
							"a=ptime:20" CRLF						\
							"a=control:streamid=%d" CRLF            \
							, sink->pt, sink->frequency, subsession->id); 
			break;	
#if 0
		case(AV_CODEC_ID_MP4A_LATM):
			sprintf(string, "a=rtpmap:%d mpeg4-generic/%d/%d" CRLF     \
							"a=fmtp:%d streamtype=5; profile-level-id=15; mode=AAC-hbr%s; sizeLength=13; indexLength=3; indexDeltaLength=3; constantDuration=1024; Profile=1"  CRLF         \
							"a=control:streamid=%d" CRLF \
							/*	  "a=type:broadcast"  CRLF \*/
							, sink->pt + subsession->id, sink->frequency, sink->nb_channels, sink->pt + subsession->id, config? config:"", subsession->id);  
			break;
		case(AV_CODEC_ID_MP4V_ES):
			sprintf(string, "a=rtpmap:%d MPEG4-ES/%d" CRLF     \
							"a=control:streamid=%d" CRLF \
							"a=fmtp:%d profile-level-id=1%s"  CRLF         \
							, sink->pt + subsession->id, sink->frequency, subsession->id, sink->pt + subsession->id, config? config:"");  
			break;
#endif
		default:
			break;			
	}
        sdp_strcat(buf, max_len, string);
}

void rtsp_create_sdp(struct rtsp_server *server)
{
	int i;
	u8 *unicast_addr, *connection_addr;
	u8 *sdp_buf = server->server_media.my_sdp;
	int max_len = server->server_media.my_sdp_max_len;
	struct rtsp_session_info *s = &server->server_media.session_info;
	rtsp_sm_subsession *subsession = NULL;
	u8 nettype[] = "IN";
	u8 addrtype[] = "IP4";
	unicast_addr = server->server_ip;
	connection_addr = server->client_ip;
	//sdp session level
	/* fill Protocol Version -- only have Version 0 for now*/	
	sprintf(sdp_buf, "v=0" CRLF);
	sdp_fill_o_field(sdp_buf, max_len, s->user, s->session_id, s->version, nettype, addrtype, unicast_addr);
	sdp_fill_s_field(sdp_buf, max_len, s->name);
	sdp_fill_c_field(sdp_buf, max_len, nettype, addrtype, connection_addr, server->message.transport.ttl);
	sdp_fill_t_field(sdp_buf, max_len, s->start_time, s->end_time);	
	//sdp media level
	list_for_each_entry(subsession, &server->server_media.media_entry, media_anchor, rtsp_sm_subsession)
	{
		//fill subsession sdp descriptions
		if(subsession->sink->pt == RTP_PT_DYN_BASE)
			sdp_fill_m_field(sdp_buf, max_len, subsession->sink->media_type, 0, subsession->id + subsession->sink->pt);
		else
			sdp_fill_m_field(sdp_buf, max_len, subsession->sink->media_type, 0, subsession->sink->pt);
		sdp_fill_subsession_a_field(sdp_buf, max_len, subsession);
	}
        server->server_media.my_sdp_content_len = strlen(sdp_buf);
}

int rtsp_on_req_DESCRIBE(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter))
{
	u8 response[1024] = {0};
	if(server->CSeq_now > server->message.CSeq)
        {
                RTSP_WARN("CSeq out of order");
		return -EINVAL;
        }
	server->CSeq_now = server->message.CSeq;       
	if(server->state_now != RTSP_INIT)
	{
		RTSP_WARN("illogical request!");
		return -EINVAL;
	}
	if(rtsp_req_cb)
            rtsp_req_cb(server->adapter->ext_adapter);
	rtsp_session_info_set(&server->server_media.session_info, 0, 0, NULL, NULL, NULL, 0, 0, 0); //use default session settings;
	//read sdp if any or create general sdp 
	if(server->server_media.my_sdp == NULL || server->server_media.my_sdp_max_len == 0)
	{
		RTSP_ERROR("no sdp buffer allocated!");
		return -ENOMEM;
	}
	if(server->server_media.my_sdp_content_len == 0 || *server->server_media.my_sdp == '\0')
		rtsp_create_sdp(server);
	sprintf(response, RTSP_RES_OK CRLF \
						"CSeq: %d" CRLF \
						"Content-Type: application/sdp" CRLF \
						"Content-Base: rtsp://%d.%d.%d.%d/test.sdp" CRLF \
						"Content-Length: %d" CRLF \
						CRLF \
						"%s", server->CSeq_now, server->server_ip[0], server->server_ip[1], server->server_ip[2], server->server_ip[3], server->server_media.my_sdp_content_len, server->server_media.my_sdp);
        //rtsp_res_dump(response, strlen(response));	
        return write(server->client_socket, response, strlen(response));
}

int rtsp_on_req_GET_PARAMETER(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter))
{
	u8 response[512] = {0};
	if(server->CSeq_now > server->message.CSeq)
        {
                RTSP_WARN("CSeq out of order");
		return -EINVAL;
        }
	server->CSeq_now = server->message.CSeq;       
	if(rtsp_req_cb)
            rtsp_req_cb(server->adapter->ext_adapter);	
	sprintf(response, RTSP_RES_OK CRLF \
						"CSeq: %d" CRLF \
						"Session: %x:timeout=%d" CRLF\
						CRLF, server->CSeq_now, server->server_media.session_info.session_id, server->server_media.session_info.session_timeout);
        //rtsp_res_dump(response, strlen(response));
        return write(server->client_socket, response, strlen(response));							
}

void rtsp_set_rtp_task(rtsp_sm_subsession *subsession, void (*rtp_task_handle)(void *ctx))
{
	subsession->rtp_task_handle = rtp_task_handle;
}

int rtsp_start_rtp_task(rtsp_sm_subsession *subsession)
{
	if(xTaskCreate(subsession->rtp_task_handle, ((const signed char*)"rtp_s_service"), 2048, (void *)subsession, RTP_SERVICE_PRIORITY, subsession->task_id) != pdPASS)
	{
		RTSP_ERROR("\n\rrtp session %d service: Create Task Error\n", subsession->id);
		return -1;;
	}	
	return 0;
}

static void rtsp_transport_check_fix(struct rtsp_transport *transport)
{
        int tmp = 0;
        //check unconfigured fields and set default value here
        if(transport->proto == TRANS_PROTO_UNKNOWN)
                transport->proto = TRANS_PROTO_RTP;
        if(transport->lower_proto == TRANS_LOWER_PROTO_UNKNOWN)
                transport->lower_proto = TRANS_LOWER_PROTO_UDP;
        if(transport->cast_mode == UNICAST_MODE)
        {
                if(transport->client_port_even == 0 || transport->client_port_odd == 0)
                {

                        rtw_mutex_get(&client_lower_port_lock);
                        tmp = rtsp_get_port(CLIENT_LOWER_PORT_BASE, 8, &client_lower_port_bitmap);
                        rtw_mutex_put(&client_lower_port_lock);
                        transport->client_port_even = (tmp%2) ? tmp+1 : tmp;
                        transport->client_port_odd = transport->client_port_even + 1;
                }
                if(transport->server_port_even == 0 || transport->server_port_odd == 0)
                {
                        rtw_mutex_get(&server_lower_port_lock);
                        tmp = rtsp_get_port(SERVER_LOWER_PORT_BASE, 8, &server_lower_port_bitmap);
                        rtw_mutex_put(&server_lower_port_lock);
                        transport->server_port_even = (tmp%2) ? tmp+1 : tmp;
                        transport->server_port_odd = transport->server_port_even + 1;
                }
        }else if(transport->cast_mode == MULTICAST_MODE)
        {
                if(transport->port_even == 0 || transport->port_odd == 0)
                {
                        rtw_mutex_get(&lower_port_lock);
                        tmp = rtsp_get_port(LOWER_PORT_BASE, 8, &lower_port_bitmap);
                        rtw_mutex_put(&lower_port_lock);
                        transport->port_even = (tmp%2) ? tmp+1 : tmp;
                        transport->port_odd = transport->port_even + 1;						
                }
                if(transport->ttl == 0 || transport->ttl >256)
                        transport->ttl = 1;
        }
        if(transport->ssrc == 0)
        {
                rtw_get_random_bytes(&transport->ssrc, sizeof(transport->ssrc));
                if(transport->ssrc < 0x10000000)
                        transport->ssrc += 0x10000000;
        }        
}

int rtsp_on_req_SETUP(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter))
{
	u8 response[512] = {0};
	p_rtsp_sm_subsession subsession = NULL;
	int iter_cnt = 0;
	if(server->CSeq_now > server->message.CSeq)
		return -EINVAL;
	server->CSeq_now = server->message.CSeq;        
	if(server->state_now != RTSP_INIT)
	{
		RTSP_WARN("illogical request!");
		return -EINVAL;
	}
	//need to clear msg record port after we copy it to respective subsession 
	list_for_each_entry(subsession, &server->server_media.media_entry, media_anchor, rtsp_sm_subsession)
	{
		iter_cnt++;
		if(!subsession->client.is_handled)
		{
			subsession->client.client_socket = server->client_socket;
			subsession->client.client_ip = server->client_ip;
			memcpy(&subsession->client.transport, &server->message.transport, sizeof(struct rtsp_transport));
                        rtsp_transport_check_fix(&subsession->client.transport);
                        //rtsp_transport_dump(&subsession->client.transport);
			//set default unicast mode for testing
			rtsp_set_rtp_task(subsession, rtp_unicast_service);
			//rtsp_set_media_handle(subsession);
			subsession->client.is_handled = 1;
                        printf("\n\rsubsession %d handled", subsession->id);
			break;
		}
	}
	if(iter_cnt >= ATOMIC_READ(&server->server_media.subsession_cnt) && subsession->client.is_handled)
		server->state_now = RTSP_READY;
	memset(&server->message.transport, 0, sizeof(struct rtsp_transport));
	if(rtsp_req_cb)
            rtsp_req_cb(server->adapter->ext_adapter);	
	if(subsession->client.transport.cast_mode == UNICAST_MODE )
	{
		if(subsession->client.transport.lower_proto == TRANS_LOWER_PROTO_UDP)
		{
			sprintf(response, RTSP_RES_OK CRLF \
                                          "CSeq: %d" CRLF \
                                          "Session: %x:timeout=%d" CRLF \
                                          "Transport: RTP/AVP/UDP;%s;client_port=%d-%d;server_port=%d-%d;ssrc=%x;mode=\"PLAY\"" CRLF \
                                          CRLF, server->CSeq_now, server->server_media.session_info.session_id, server->server_media.session_info.session_timeout, \
                                          STR_UNICAST, subsession->client.transport.client_port_even, subsession->client.transport.client_port_odd, \
                                          subsession->client.transport.server_port_even, subsession->client.transport.server_port_odd, subsession->client.transport.ssrc);
		}else if(subsession->client.transport.lower_proto == TRANS_LOWER_PROTO_TCP)
		{
			sprintf(response, RTSP_RES_OK CRLF \
                                          "CSeq: %d" CRLF \
                                          "Session: %x:timeout=%d" CRLF \
                                          "Transport: RTP/AVP/TCP;%s;client_port=%d-%d;server_port=%d-%d;ssrc=%x;mode=\"PLAY\"" CRLF \
                                          CRLF, server->CSeq_now, server->server_media.session_info.session_id, server->server_media.session_info.session_timeout, \
                                          STR_UNICAST, subsession->client.transport.client_port_even, subsession->client.transport.client_port_odd, \
                                          subsession->client.transport.server_port_even, subsession->client.transport.server_port_odd, subsession->client.transport.ssrc);			
		}else{
			RTSP_ERROR("missing param1!");
			return -EINVAL;			
		}
	}else if(subsession->client.transport.cast_mode == MULTICAST_MODE)
	{
			sprintf(response, RTSP_RES_OK CRLF \
                                          "CSeq: %d" CRLF \
                                          "Session: %x:timeout=%d" CRLF \
                                          "Transport: RTP/AVP/UDP;%s;port=%d-%d;ttl=%d;ssrc=%x;mode=\"PLAY\"" CRLF \
                                          CRLF, server->CSeq_now, server->server_media.session_info.session_id, server->server_media.session_info.session_timeout, \
                                          STR_MULTICAST, subsession->client.transport.port_even, subsession->client.transport.port_odd, subsession->client.transport.ttl, subsession->client.transport.ssrc);		
	}else{
		RTSP_ERROR("missing param2!");
		return -EINVAL;		
	}
        //rtsp_res_dump(response, strlen(response));        
	return write(server->client_socket, response, strlen(response));	
}

int rtsp_on_req_PLAY(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter))
{
	u8 response[128] = {0};
	p_rtsp_sm_subsession subsession = NULL;
        int timer = 100;
	int ret = 0;
	if(server->CSeq_now > server->message.CSeq)
        {
                RTSP_WARN("CSeq out of order");
		return -EINVAL;
        }
	server->CSeq_now = server->message.CSeq;        
	if(server->state_now != RTSP_READY)
	{
		RTSP_WARN("illogical request!");
		return -EINVAL;
	}
	if(rtsp_req_cb)
            rtsp_req_cb(server->adapter->ext_adapter);
        server->state_now = RTSP_PLAYING;	
	//start rtp session here
	list_for_each_entry(subsession, &server->server_media.media_entry, media_anchor, rtsp_sm_subsession)
	{
#if 1
                rtp_sink_packet_init(subsession->sink);
		ret = rtsp_start_rtp_task(subsession);
		if(ret < 0)
		{
			//do we need to clear resource record here?
			//server->state_now = RTSP_INIT;
			return -1;
		}
#endif
	}
        while(ATOMIC_READ(&server->server_media.reference_cnt) < ATOMIC_READ(&server->server_media.subsession_cnt))
        {
            rtw_msleep_os(10);
            if(--timer <= 0)
            {
                server->state_now = RTSP_INIT;
                sprintf(response, RTSP_RES_SNF CRLF \
                                                        "CSeq: %d" CRLF \
                                                        "Session: %x" CRLF \
                                                        CRLF, server->CSeq_now, server->server_media.session_info.session_id);	
                return write(server->client_socket, response, strlen(response));                
            }   
        }
        
	RTSP_INFO("rtp session start");
	sprintf(response, RTSP_RES_OK CRLF \
						"CSeq: %d" CRLF \
						"Session: %x" CRLF \
						CRLF, server->CSeq_now, server->server_media.session_info.session_id);
        //rtsp_res_dump(response, strlen(response));	
        return write(server->client_socket, response, strlen(response));
}

int rtsp_on_req_TEARDOWN(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter))
{
	u8 response[128] = {0};
	if(server->CSeq_now > server->message.CSeq)
        {
                RTSP_WARN("CSeq out of order");
		return -EINVAL;
        }
	server->CSeq_now = server->message.CSeq;        
	if(rtsp_req_cb)
            rtsp_req_cb(server->adapter->ext_adapter);	
	server->state_now = RTSP_INIT;
	sprintf(response, RTSP_RES_OK CRLF \
						"CSeq: %d" CRLF \
						"Session: %x" CRLF \
						CRLF, server->CSeq_now, server->server_media.session_info.session_id);
        //rtsp_res_dump(response, strlen(response));
        return write(server->client_socket, response, strlen(response));
}

int rtsp_on_req_PAUSE(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter))
{
	u8 response[128] = {0};
	if(server->CSeq_now > server->message.CSeq)
        {
                RTSP_WARN("CSeq out of order");
		return -EINVAL;
        }
	server->CSeq_now = server->message.CSeq;        
	if(rtsp_req_cb)
            rtsp_req_cb(server->adapter->ext_adapter);	
	server->state_now = RTSP_READY;
	sprintf(response, RTSP_RES_OK CRLF \
						"CSeq: %d" CRLF \
						"Session: %x" CRLF \
						CRLF, server->CSeq_now, server->server_media.session_info.session_id);
        //rtsp_res_dump(response, strlen(response));	
        return write(server->client_socket, response, strlen(response));	
}

int rtsp_on_req_UNDEFINED(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter))
{
	u8 response[128] = {0};
	if(server->CSeq_now > server->message.CSeq)
        {
                RTSP_WARN("CSeq out of order");
		return -EINVAL;
        }
	server->CSeq_now = server->message.CSeq;        
	if(rtsp_req_cb)
            rtsp_req_cb(server->adapter->ext_adapter);
        server->state_now = RTSP_INIT;
	sprintf(response, RTSP_RES_BAD CRLF \
						"CSeq: %d" CRLF \
						CRLF, server->CSeq_now);
	return write(server->client_socket, response, strlen(response));	
}

static int rtsp_check_wifi_connectivity(const char *ifname, int *mode)
{
		if(rltk_wlan_running(0)>0){
			wext_get_mode(ifname, mode);
			if(wifi_is_ready_to_transceive(RTW_STA_INTERFACE) >= 0 && (*mode == IW_MODE_INFRA)){
			  RTSP_INFO("connect successful sta mode\r\n");
			  return 0;
			}
			if(wifi_is_ready_to_transceive(RTW_AP_INTERFACE) >= 0 && (*mode == IW_MODE_MASTER)){
			  RTSP_INFO("connect successful ap mode\r\n");
			  return 0;
			}			
		}
		return -1;
}

void rtsp_server_service(void *ctx)
{
		struct rtsp_server *server = (struct rtsp_server *)ctx;
		u8 *request;
		int opt = 1;
		int mode = 0;
		int ret;
		u32 time_base, time_now;
		struct sockaddr_in server_addr, client_addr;
                socklen_t client_addr_len = sizeof(struct sockaddr_in);
		fd_set server_read_fds, client_read_fds;
		struct timeval s_listen_timeout, c_listen_timeout;
                if((request = malloc(REQUEST_BUF_SIZE)) == NULL)
                {
                        RTSP_ERROR("rtsp request buffer allocate fail");
                        goto exit;
                }
//first check wifi connectivity
restart:
		time_base = rtw_get_current_time();
		while(rtsp_check_wifi_connectivity(WLAN0_NAME, &mode) < 0)
		{
			time_now = rtw_get_current_time();
			if((time_now - time_base) > rtsp_launch_timeout)
			{
				RTSP_ERROR("rtsp service time out - wifi not ready");
				goto exit;
			}
                        rtw_msleep_os(10);
		}
//socket init
		server->server_socket = socket(AF_INET, SOCK_STREAM, 0);
		if(server->server_socket < 0)
		{
				RTSP_ERROR("\n\rrtsp server socket create failed");
				goto exit;
		}
		server->server_ip = LwIP_GetIP(&xnetif[0]);
		if((setsockopt(server->server_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt))) < 0){
			RTSP_ERROR("\r\n Error on setting socket option");
			goto exit1;
		}		
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = *(uint32_t *)(server->server_ip); /*_htonl(INADDR_ANY)*/
		server_addr.sin_port = _htons(server->server_port);
		if(bind(server->server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
		{
			RTSP_ERROR("\n\rcannot bind stream socket");
			goto exit1;
		}
		listen(server->server_socket, 1);
                //indicate server launched
                server->is_launched = 1;
                RTSP_WARN("rtsp server start...");
		//enter service loop
		while(server->is_launched)
		{
			ret = 0;
			FD_ZERO(&server_read_fds);
			s_listen_timeout.tv_sec = 1;
			s_listen_timeout.tv_usec = 0;
			FD_SET(server->server_socket, &server_read_fds);
			if(select(RTSP_SELECT_SOCK, &server_read_fds, NULL, NULL, &s_listen_timeout))
			{
				server->client_socket = accept(server->server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
				if(server->client_socket < 0)
				{
					RTSP_ERROR("\n\rcleint socket error");
					close(server->client_socket);
					continue;
				}
				//load client ip address from client_addr
				*(u32 *)server->client_ip = client_addr.sin_addr.s_addr;
                                //printf("\n\rclient ip:%x", *(u32 *)server->client_ip);
				//enter negotiation process
				while(server->is_launched)
				{
					FD_ZERO(&client_read_fds);
					c_listen_timeout.tv_sec = 0;
					c_listen_timeout.tv_usec = 10000;
					FD_SET(server->client_socket, &client_read_fds);
					
					if(select(RTSP_SELECT_SOCK, &client_read_fds, NULL, NULL, &c_listen_timeout))
					{
						memset(request, 0, REQUEST_BUF_SIZE);
						ret = read(server->client_socket, request, REQUEST_BUF_SIZE);
                                                //rtsp_req_dump(request, ret);
						//check and parse request
						//if(rtsp_parse_request(&server->message, request, ret) == -EAGAIN)
                                                //  continue;
                                                if(rtsp_parse_request(&server->message, request, ret) < 0)
                                                    goto out;
                                                switch(server->message.method)
                                                {
                                                        case(RTSP_REQ_OPTIONS):
                                                                ret = rtsp_on_req_OPTIONS(server, rtsp_req_OPTIONS_cb);
                                                                break;
                                                        case(RTSP_REQ_DESCRIBE):
                                                                ret = rtsp_on_req_DESCRIBE(server, rtsp_req_DESCRIBE_cb);
                                                                break;
                                                        case(RTSP_REQ_GET_PARAMETER):
                                                                ret = rtsp_on_req_GET_PARAMETER(server, rtsp_req_GET_PARAMETER_cb);
                                                                break;
                                                        case(RTSP_REQ_SETUP):
                                                                ret = rtsp_on_req_SETUP(server, rtsp_req_SETUP_cb);
                                                                break;
                                                        case(RTSP_REQ_PLAY):
                                                                ret = rtsp_on_req_PLAY(server, rtsp_req_PLAY_cb);
                                                                break;
                                                        case(RTSP_REQ_TEARDOWN):
                                                                ret = rtsp_on_req_TEARDOWN(server, rtsp_req_TEARDOWN_cb);
                                                                break;
                                                        case(RTSP_REQ_PAUSE):
                                                                ret = rtsp_on_req_PAUSE(server, rtsp_req_PAUSE_cb);
                                                                break;
                                                        default:
                                                                ret = rtsp_on_req_UNDEFINED(server, rtsp_req_UNDEFINED_cb);
                                                                break;
                                                }
                                                if(ret < 0)
                                                {
                                                                RTSP_ERROR("\n\rrtsp send response failed - err code:%d", ret);
                                                                goto out;
                                                }
					}
					if(rtsp_check_wifi_connectivity(WLAN0_NAME, &mode) < 0)
						goto out;
				}
out:			
				close(server->client_socket);
                                rtsp_sm_session_refresh(&server->server_media);
				server->state_now = RTSP_INIT;				
			}
			
			if(rtsp_check_wifi_connectivity(WLAN0_NAME, &mode) < 0)
			{
				RTSP_WARN("\n\rwifi Tx/Rx broke!");
				close(server->server_socket);
				RTSP_WARN("\n\rRTSP server restart in %ds...", rtsp_launch_timeout/1000);
				goto restart;
			}
			rtw_msleep_os(1000);
		}
exit1:
		rtsp_server_stop(server);
		close(server->server_socket);
                free(request);
                RTSP_WARN("rtsp server stop...");
exit:                
		vTaskDelete(NULL);
}

static void rtsp_server_set_launch_handle(struct rtsp_server *server, void (*func)(void *ctx))
{
		server->launch_handle = func;
}

int rtsp_server_launch(struct rtsp_server *server)
{
		if(!server)
		{
			RTSP_WARN("\n\rserver invalid");
			return -1;
		}
		if(server->is_launched)
		{
			RTSP_WARN("\n\rserver launched (permission denied)");
			return -1;
		}
		if(!server->is_setup)
		{
			RTSP_WARN("\n\rserver not set up (permission denied)");
			return -1;
		}
		//start rtsp server task
		rtsp_server_set_launch_handle(server, &rtsp_server_service);
		if(xTaskCreate(*server->launch_handle, ((const signed char*)"rtsp_s_service"), 1024, (void *)server, RTSP_SERVICE_PRIORITY, server->rtsp_task_id) != pdPASS)
		{
			RTSP_ERROR("\n\rrtsp server service: Create Task Error\n");
			goto error;
		}
		
		return 0;
error:
		server->rtsp_task_id = NULL;
		return -1;
}

void rtsp_server_stop(struct rtsp_server *server)
{
		server->is_launched = 0;
}

_WEAK int rtsp_req_OPTIONS_cb(void *ext_adapter)
{
    return 0;
};

_WEAK int rtsp_req_DESCRIBE_cb(void *ext_adapter)
{
    return 0;
}

_WEAK int rtsp_req_GET_PARAMETER_cb(void *ext_adapter)
{
    return 0;
}

_WEAK int rtsp_req_SETUP_cb(void *ext_adapter)
{
    return 0;
}

_WEAK int rtsp_req_PLAY_cb(void *ext_adapter)
{
    return 0;
}

_WEAK int rtsp_req_TEARDOWN_cb(void *ext_adapter)
{
    return 0;
}

_WEAK int rtsp_req_PAUSE_cb(void *ext_adapter)
{
    return 0;
}

_WEAK int rtsp_req_UNDEFINED_cb(void *ext_adapter)
{
    return 0;
}

