#ifndef _RTSP_SERVER_H_
#define _RTSP_SERVER_H_

/*****************************************************INCLUDE**************************************************/
#include "task.h"
#include "rtp_common.h"
#include "rtsp_common.h"
#include "rtp_sink.h"
#include "rtp_source.h"

/*****************************************************DEFINITIONS**********************************************/

/* some quick defintion */
#ifndef CRLF
#define CRLF	"\r\n"
#endif

inline int is_line_end(u8 *x)
{
    return (*x=='\n' || *x=='\0');
}

#define RTSP_PORT_DEF 554
#define RTSP_SELECT_SOCK 8
#define MAX_URL_LEN	32
#define REQUEST_BUF_SIZE	1024
#define RESPONSE_BUF_SIZE	1024

#define LOWER_PORT_BASE		51100
#define CLIENT_LOWER_PORT_BASE 	51200
#define SERVER_LOWER_PORT_BASE	51400

#define DEF_SESSION_TIMEOUT	(60000) //in ms

/*****************************************************STRUCTURES***********************************************/

enum _rtsp_state {
    RTSP_INIT = 0,
    RTSP_READY = 1,
    RTSP_PLAYING = 2,
};
typedef enum _rtsp_state rtsp_state;

typedef struct _rtsp_client_connection_session{
	int client_socket;
	u8 *client_ip;
	struct rtsp_transport transport;
	u8 is_handled;
}rtsp_cc_session, *p_rtsp_cc_session;

typedef struct _rtsp_server_media_subsession{
	u32 id;
	void *parent_session;
	_list media_anchor;
	rtp_source_t *src;
	rtp_sink_t *sink;
	rtsp_cc_session client;
	TaskHandle_t task_id;
	void (*rtp_task_handle)(void *ctx); //we register rtp task here
	u8* my_sdp;
	int my_sdp_max_len;
	int my_sdp_content_len;	
}rtsp_sm_subsession, *p_rtsp_sm_subsession;

typedef struct _rtsp_server_media_session{
	_list media_entry;
	void *parent_server;
	u8* my_sdp;
	int my_sdp_max_len;
	int my_sdp_content_len;
	int max_subsession_nb;
	ATOMIC_T subsession_cnt;
	ATOMIC_T reference_cnt;
	struct rtsp_session_info session_info;
	//u32 time_stamp; // "Timestamp:[digit][.delay]"	
}rtsp_sm_session, *p_rtsp_sm_session;

typedef struct _rtsp_client_media_subsession{
	u32 id;
	void *parent_session;
	_list media_anchor;
	rtp_source_t *src;
	rtp_sink_t *sink;	
}rtsp_cm_subsession, *p_rtsp_cm_subsession;

typedef struct _rtsp_client_media_session{
	_list media_entry;
	void *parent_client;
	u32 subsession_cnt;
}rtsp_cm_session, *p_rtsp_cm_session;

//struct to store basic configuration for create rtsp server
typedef struct _rtsp_server_adapter
{
	int max_subsession_nb;
	void *ext_adapter;
}rtsp_server_adapter;

struct rtsp_server{
	TaskHandle_t rtsp_task_id;
	void (*launch_handle)(void *ctx);
	rtsp_server_adapter *adapter;
	u8 is_setup;
	u8 is_launched;
	u8 server_url[MAX_URL_LEN];
	int server_socket;
	u16 server_port;
	u8 *server_ip;
	int client_socket;
	u8 *client_ip;
	struct rtsp_message message;
	u32 CSeq_now;
	rtsp_state state_now;
	rtsp_sm_session server_media;
};

extern int rtsp_req_OPTIONS_cb(void *ext_adapter);
extern int rtsp_req_DESCRIBE_cb(void *ext_adapter);
extern int rtsp_req_GET_PARAMETER_cb(void *ext_adapter);
extern int rtsp_req_SETUP_cb(void *ext_adapter);
extern int rtsp_req_PLAY_cb(void *ext_adapter);
extern int rtsp_req_TEARDOWN_cb(void *ext_adapter);
extern int rtsp_req_PAUSE_cb(void *ext_adapter);
extern int rtsp_req_UNDEFINED_cb(void *ext_adapter);

int rtsp_on_req_OPTIONS(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter));
int rtsp_on_req_DESCRIBE(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter));
int rtsp_on_req_GET_PARAMETER(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter));
int rtsp_on_req_SETUP(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter));
int rtsp_on_req_PLAY(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter));
int rtsp_on_req_TEARDOWN(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter));
int rtsp_on_req_PAUSE(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter));
int rtsp_on_req_UNDEFINED(struct rtsp_server *server, int (*rtsp_req_cb)(void *ext_adapter));


int rtsp_parse_request(struct rtsp_message *msg, u8 *request, int size);
void rtsp_sm_subsession_free(rtsp_sm_subsession *subsession);
void rtsp_sm_session_free(rtsp_sm_session *session);
int rtsp_sm_subsession_add(rtsp_sm_session *session, rtsp_sm_subsession *subsession);
rtsp_sm_subsession *rtsp_sm_subsession_create(rtp_source_t *src, rtp_sink_t *sink, int max_sdp_size);
void rtsp_sm_clear_session(rtsp_sm_session *session);
void rtsp_sm_clear_all(rtsp_sm_session *session);
int rtsp_sm_setup(rtsp_sm_session *session, void *parent, int max_subsession_nb, int max_sdp_size);
struct rtsp_server *rtsp_server_create(rtsp_server_adapter *adapter);
void rtsp_server_free(struct rtsp_server *server);
void rtsp_server_stop(struct rtsp_server *server);
int rtsp_server_setup(struct rtsp_server *server, const u8* server_url, int port);
int rtsp_server_launch(struct rtsp_server *server);

#endif