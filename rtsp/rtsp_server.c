#include "FreeRTOS.h"
#include "task.h"
#include "platform/platform_stdlib.h"
#include "rtsp_rtp_dbg.h"
#include "rtsp_common.h"
#include "rtsp_server.h"

#include "sockets.h" //for sockets
#include "wifi_conf.h"

#define RTSP_IP_SIZE	4

static rtsp_launch_timeout = 60000; //in ms

static u8 lower_port_bitmap = 0;
static _mutex lower_port_lock = NULL;
static u8 client_lower_port_bitmap = 0;
static _mutex client_lower_port_lock = NULL;
static u8 server_lower_port_bitmap = 0;
static _mutex server_lower_port_lock = NULL;

//this range param should be aligned with port bitmap type size
static int rtsp_get_port(int base, int range, u8* bitmap)
{
	int find = -1;
	for(int i = 0; i < range; i += 2)
	{
		if(*bitmap&(1 << i) == 0)
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
	int bit = (port - base)/2;
	if(*bitmap&(1 << bit) == 1)
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
		while(*p_end != ':' && !IS_LINE_END(p_end) && offset <= *size_left)
		{
			p_end++;
			offset++;
		}
		if(*p_end != ':')//invalid header
		{
			*size_left = 0;
			return NULL;
		}
		//header type check
		memset(b_tmp, 0, 16);
		memcpy(b_tmp, p_tmp, p_end - p_tmp);
		b_tmp[p_end - p_tmp] = '\0';
		if(strncmp(b_tmp, "Request", 7) != 0)//invalid header
		{
			*size_left = 0;
			return NULL;
		}
		p_end++;
		offset++;
		p_tmp = p_end; //skip space
		while(!IS_LINE_END(p_end) && offset <= *size_left)
		{
			if(*p_end == ' ')
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
											msg->method = RTSP_REQ_PAUSE
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
		if(*p_end == '\r')
		{
			//copy request line without CRLF
			if(p_end-p_start < REQ_LINE_BUF_SIZE)
			{
				memcpy(msg->request_line, p_start, p_end-p_start);
				msg->request_line[p_end-p_start] = '\0';
			}
			else
			{
				memcpy(msg->request_line, p_start, REQ_LINE_BUF_SIZE - 1);
				msg->request_line[REQ_LINE_BUF_SIZE - 1] = '\0';
			}
			//skip CRLF
			p_end += 2;
			offset += 2;
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
		int tmp = 0;
		char delimeter [] = "/=-";
		u8 b_tmp[64] = {0};
		p_start = p_tmp = p_end = body;
		while(!IS_LINE_END(p_end) && offset <= *size_left)
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
			while(!IS_LINE_END(p_end) && offset <= *size_left)
			{
				if(*p_end == '\r')
				{
					memcpy(b_tmp, p_tmp, p_end - p_tmp);
					b_tmp[p_end - p_tmp] = '\0';
					msg->CSeq = atoi(b_tmp);
					p_end = p_end + 2; //skip CRLF
					offset += 2;
					break;
				}
				p_end++;
				offset++;
			}
		}else if(!strncmp(b_tmp, "Transport", 9))
			{
				while(!IS_LINE_END(p_end) && offset <= *size_left)
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
							p_end += 2;
							offset += 2;
							p_tmp = p_end;//skip CRLF
						}
					}else{
						p_end++;
						offset++;
					}
				}
				//to do check unconfigured fields and set default value here
				if(msg->transport.proto == TRANS_PROTO_UNKNOWN)
					msg->transport.proto = TRANS_PROTO_RTP;
				if(msg->transport.lower_proto == TRANS_LOWER_PROTO_UNKNOWN)
					msg->transport.lower_proto = TRANS_LOWER_PROTO_UDP;
				if(msg->transport.cast_mode == UNICAST_MODE)
				{
					if(msg->transport.client_port_even == 0 || msg->transport.client_port_odd == 0)
					{
						rtw_mutex_get(&client_lower_port_lock);
						tmp = rtsp_get_port(CLIENT_LOWER_PORT_BASE, 8, &client_lower_port_bitmap);
						rtw_mutex_put(&client_lower_port_lock);
						msg->transport.client_port_even = (tmp%2) ? tmp+1 : tmp;
						msg->transport.client_port_odd = msg->transport.client_port_even + 1;
					}
					if(msg->transport.server_port_even == 0 || msg->transport.server_port_odd == 0)
					{
						rtw_mutex_get(&server_lower_port_lock);
						tmp = rtsp_get_rand(SERVER_LOWER_PORT_BASE, 8, &server_lower_port_bitmap);
						rtw_mutex_put(&server_lower_port_lock);
						msg->transport.server_port_even = (tmp%2) ? tmp+1 : tmp;
						msg->transport.server_port_odd = msg->transport.server_port_even + 1;
					}
					if(msg->ssrc == 0)
						msg->ssrc = rtw_get_current_time();
				}else if(msg->transport.cast_mode == MULTICAST_MODE)
				{
					if(msg->transport.port_even == 0 || msg->transport.port_odd == 0)
					{
						rtw_mutex_get(&lower_port_lock);
						tmp = rtsp_get_port(LOWER_PORT_BASE, 8, &lower_port_bitmap);
						rtw_mutex_put(&lower_port_lock);
						msg->transport.port_even = (tmp%2) ? tmp+1 : tmp;
						msg->transport.port_odd = msg->transport.port_even + 1;						
					}
					if(msg->transport.ttl == 0 || msg->transport.ttl >256)
						msg->transport.ttl = 1;
				}
				
			}else{
					while(!IS_LINE_END(p_end) && offset <= *size_left)
					{
						p_end ++;
						offset ++;
					}
				}
		if(*p_end == '\r')
		{
			//skip CRLF
			p_end += 2;
			offset += 2;
			*size_left -= offset;
			return p_end;
		}else{
			*size_left = 0;
			return NULL;
		}
}

int rtsp_parse_request(struct rtsp_message *msg, u8 *request, int size)
{
		u8 *p_start = *p_body = NULL;
		int size_left = size;
		//is it a rtsp request?
		if(request == NULL || *request == '\0' || size <= 0)
		{
			RTSP_WARN("\n\rinvalid request - (NULL request)");
			return -1;
		}
		p_start = request;
		p_body = rtsp_parse_header_line(msg, p_start, &size_left);
		if(msg->method == RTSP_REQ_UNDEFINED)
		{
			RTSP_WARN("\n\rinvalid request - (UNDEFINED request)");
			return -1;
		}			
		while(p_body != NULL && *p_body != '\0' && size_left > 0)
		{
			p_body = rtsp_parse_body_line(msg, p_body, &size_left);
		}
		return 0;
}

/* rtsp server media session */

void rtsp_sm_subsession_free(rtsp_sm_subsession *subsession)
{
		if(subsession->my_sdp != NULL)
			free(subsession->my_sdp);
		if(subsession != NULL)
			free(subsession);
}

rtsp_sm_subsession * rtsp_sm_subsession_create(struct rtp_source *src, struct rtp_sink *sink, int max_sdp_size)
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
		if(session->subsession_cnt >= session->max_subsession_nb)
		{
			RTSP_WARN("\n\rmax subsession cnt reached!");
			return -1;
		}
		subsession->id = session->subsession_cnt;
		list_add_tail(&subsession->media_anchor, &session->media_entry);
		subsession->parent_session = (void *)session;
		session->subsession_cnt++;
		return 0;
}

void rtsp_server_media_clear_session(rtsp_sm_session *session)
{
		INIT_LIST_HEAD(&session->media_entry);
		session->my_sdp_content_len = 0;
		session->subsession_cnt = 0;
		session->reference_cnt = 0;			
}

void rtsp_server_media_clear_all(rtsp_sm_session *session)
{
		if(session->my_sdp != NULL)
			free(session->my_sdp);
		session->parent_server = NULL;
		INIT_LIST_HEAD(&session->media_entry);
		session->my_sdp_max_len = 0;
		session->my_sdp_content_len = 0;
		session->subsession_cnt = 0;
		session->reference_cnt = 0;		
}

int rtsp_server_media_setup(rtsp_sm_session *session, void *parent, int max_sdp_size)
{
		session->parent_server = parent;
		if((session->my_sdp = malloc(max_sdp_size)) == NULL)
		{
			RTSP_ERROR("\n\rcreate media sdp buffer failed");
			return -1;
		}
		session->my_sdp_max_len = max_sdp_size;
		INIT_LIST_HEAD(&session->media_entry);
		session->my_sdp_content_len = 0;
		session->subsession_cnt = 0;
		session->reference_cnt = 0;
		return 0;
}

/* end of rtsp server media session */

struct rtsp_server *rtsp_server_create(rtsp_adapter *adapter)
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
		if(rtsp_server_media_setup(&server->server_media, (void *)server, MAX_SDP_SIZE) < 0)
		{
			RTSP_ERROR("\n\rmedia setup failed");
			free(server->server_ip);
			free(server);
			return NULL;
		}
		server->server_socket = -1;
		server->ext_adapter = adapter->ext_adapter;
		server->server_media.max_subsession_nb = (adapter->max_subsession_nb <= 0) ? 1 : adapter->max_subsession_nb;
		return server;
}

int rtsp_server_setup(struct rtsp_server *server, const u8* server_url, int port)
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
		if(list_empty(&server->server_media.media_entry))
		{
			RTSP_WARN("\n\rmedia not ready (permission denied)");
			return -1;
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

int rtsp_on_req_OPTIONS(struct rtsp_server *server, void *cb)
{
	u8 response[256] = {0};
	if(server->CSeq_now == server->message.CSeq && server->CSeq_now != 0)
		return 0;
	server->CSeq_now = server->message.CSeq;
	if(cb)
		cb(server);
	sprintf(response, RTSP_RES_OK CRLF \
						"CSeq: %d" CRLF \
						PUBLIC_CMD_STR CRLF \
						CRLF, server->CSeq_now);
	return write(server->client_connection.client_socket, response, strlen(response));
}

static void rtsp_session_set(struct rtsp_session *s, u32 session_id, u32 session_timeout, u8 *user, u8 *name, u8 *info, u32 version, u64 start_time, u64 end_time)
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

static int get_frequency_index(int samplerate)
{
	uint32_t freq_idx_map[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
	for(int i=0;i<sizeof(freq_idx_map)/sizeof(freq_idx_map[0]);i++){
		if(samplerate==freq_idx_map[i])
			return i;
	}
	return 0xf;		// 15: frequency is written explictly 
}

void rtsp_create_sdp(struct rtsp_server *server)
{
	int i;
	u8 *unicast_addr, *connection_addr;
	u8 *sdp_buf = server->server_media.my_sdp;
	int max_len = server->server_media.my_sdp_max_len;
	struct rtsp_session *s = &server->server_media.session;
	u8 nettype[] = "IN";
	u8 addrtype[] = "IP4";
	unicast_addr = server->server_ip;
	connection_addr = server->client_connection.client_ip;
	//sdp session level
	/* fill Protocol Version -- only have Version 0 for now*/	
	sprintf(sdp_buf, "v=0" CRLF);
	sdp_fill_o_field(sdp_buf, max_len, s->user, s->session_id, s->version, nettype, addrtype, unicast_addr);
	sdp_fill_s_field(sdp_buf, max_len, s->name);
	sdp_fill_c_field(sdp_buf, max_len, nettype, addrtype, connection_addr, server->message.transport.ttl);
	sdp_fill_t_field(sdp_buf, max_len, rtsp_ctx->session.start_time, rtsp_ctx->session.end_time);	
	//sdp media level
	for(i = 0; i < server->server_media.subsession_cnt; i++)
	{
		
		
	}
}

int rtsp_on_req_DESCRIBE(struct rtsp_server *server, void *cb)
{
	u8 response[1024] = {0};
	if(server->state_now != RTSP_INIT)
	{
		RTSP_WARN("illogical request!");
		return -1;
	}
	if(cb)
		cb(server);
	rtsp_session_set(&server->server_media.session, 0, 0, NULL, NULL, NULL, 0, 0, 0); //use default session settings;
	//read sdp if any or create general sdp 
	if(server->server_media.my_sdp == NULL || server->server_media.my_sdp_max_len == 0)
	{
		RTSP_ERROR("no sdp buffer allocated!");
		return -1;
	}
	if(server->server_media.my_sdp_content_len == 0 || *server->server_media.my_sdp == '\0')
		rtsp_create_sdp(server);
	sprintf(response, RTSP_RES_OK CRLF \
						"CSeq: %d" CRLF \
						"Content-Type: application/sdp" CRLF \
						"Content-Base: rtsp://%d.%d.%d.%d/test.sdp" CRLF \
						"Content-Length: %d" CRLF \
						CRLF \
						"%s", server->CSeq_now, server->server_ip[0], server->server_ip[1], server->server_ip[2], server->server_ip[3], /*sdp length*/, /*sdp_content*/);
	return write(server->client_connection.client_socket, response, strlen(response));
}

int rtsp_on_req_GET_PARAMETER(struct rtsp_server *server, void *cb)
{
	u8 response[512] = {0};
	if(cb)
		cb(server);	
}

int rtsp_on_req_SETUP(struct rtsp_server *server, void *cb)
{
	
	if(cb)
		cb(server);	
}

int rtsp_on_req_PLAY(struct rtsp_server *server, void *cb)
{
	u8 response[128] = {0};
	if(cb)
		cb(server);	
	sprintf(response, RTSP_RES_OK CRLF \
						"CSeq: %d" CRLF \
						"Session: %x" CRLF \
						CRLF, server->CSeq_now, server->message.session_id);
	return write(server->client_connection.client_socket, response, strlen(response));
}

int rtsp_on_req_TEARDOWN(struct rtsp_server *server, void *cb)
{
	u8 response[128] = {0};
	if(cb)
		cb(server);	
	sprintf(response, RTSP_RES_OK CRLF \
						"CSeq: %d" CRLF \
						"Session: %x" CRLF \
						CRLF, server->CSeq_now, server->message.session_id);
	return write(server->client_connection.client_socket, response, strlen(response));
}

int rtsp_on_req_PAUSE(struct rtsp_server *server, void *cb)
{
	u8 response[128] = {0};
	if(cb)
		cb(server);	
	sprintf(response, RTSP_RES_OK CRLF \
						"CSeq: %d" CRLF \
						"Session: %x" CRLF \
						CRLF, server->CSeq_now, server->message.session_id);
	return write(server->client_connection.client_socket, response, strlen(response));	
}

int rtsp_on_req_UNDEFINED(struct rtsp_server *server, void *cb)
{
	u8 response[128] = {0};
	if(cb)
		cb(server);	
	sprintf(response, RTSP_RES_BAD CRLF \
						"CSeq: %d" CRLF \
						"Session: %x" CRLF \
						CRLF, server->CSeq_now, server->message.session_id);
	return write(server->client_connection.client_socket, response, strlen(response));	
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
		u8 request[REQUEST_BUF_SIZE];
		int opt = 1;
		int mode = 0;
		int ret;
		u32 time_base, time_now;
		struct sockaddr_in server_addr, client_addr;
		fd_set server_read_fds, client_read_fds;
		struct timeval s_listen_timeout, c_listen_timeout;
//first check wifi connectivity
restart:
		time_base = rtw_get_current_time();
		while(rtsp_check_wifi_connectivity(WLAN0_NAME, &mode) < 0)
		{
			time_now = rtw_get_current_time();
			if((time_now - time_base) > rtsp_launch_timeout)
			{
				RTSP_ERROR("\n\rrtsp service time out - wifi not ready");
				goto exit;
			}
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
		server_addr.sin_addr.s_addr = *(uint32_t *)(rtsp_ctx->connect_ctx.server_ip); /*_htonl(INADDR_ANY)*/
		server_addr.sin_port = _htons(server->server_port);
		if(bind(server->server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
		{
			RTSP_ERROR("\n\rcannot bind stream socket");
			goto exit1;
		}
		listen(server->server_socket, 1);
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
				server->client_connection->client_socket = accept(server->server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
				if(server->client_connection->client_socket < 0)
				{
					RTSP_ERROR("\n\rcleint socket error");
					close(server->client_connection->client_socket);
					continue;
				}
				//load client ip address and port from client_addr
				
				//enter negotiation process
				while(server->is_launched)
				{
					FD_ZERO(&client_read_fds);
					c_listen_timeout.tv_sec = 0;
					c_listen_timeout.tv_usec = 10000;
					FD_SET(server->client_connection->client_socket, &client_read_fds);
					
					if(select(RTSP_SELECT_SOCK, &client_read_fds, NULL, NULL, &c_listen_timeout))
					{
						memset(request, 0, REQUEST_BUF_SIZE);
						ret = read(server->client_connection->client_socket, request, REQUEST_BUF_SIZE);
						//check and parse request
						rtsp_parse_request(&server->message, request, ret);
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
								//need to exit??
						}
					}
					if(rtsp_check_wifi_connectivity(WLAN0_NAME, &mode) < 0)
						goto out;
				}
out:			
				close(server->client_connection.client_socket);	
				server->state_now = RTSP_INIT;				
			}
			
			if(rtsp_check_wifi_connectivity(WLAN0_NAME, &mode) < 0)
			{
				RTSP_WARN("\n\rwifi Tx/Rx broke!");
				close(server->server_socket);
				RTSP_WARN("\n\rRTSP server restart in %ds...", rtsp_launch_timeout/1000);
				goto restart;
			}
			//check ret value if see if goto exit1
			
		}
exit1:
		rtsp_server_stop(server);
		close(server->server_socket);
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
		rtsp_server_set_launch_handle(server, rtsp_server_service);
		if(xTaskCreate(server->launch_handle, ((const signed char*)"rtsp_s_service"), 2048, (void *)server, RTSP_SERVICE_PRIORITY, server->rtsp_task_id) != pdPASS)
		{
			RTSP_ERROR("\n\rrtsp server service: Create Task Error\n");
			goto error;
		}
		server->is_launched = 1;
		return 0;
error:
		server->rtsp_task_id = NULL;
		return -1;
}

void rtsp_server_stop(struct rtsp_server *server)
{
		server->is_launched = 0;
}