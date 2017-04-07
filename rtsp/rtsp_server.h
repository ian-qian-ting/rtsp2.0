#ifndef _RTSP_SERVER_H_
#define _RTSP_SERVER_H_

/*****************************************************INCLUDE**************************************************/
#include "rtp_common.h"
#include "rtsp_common.h"

/*****************************************************DEFINITIONS**********************************************/

/* some quick defintion */
#ifndef CRLF
#define CRLF	"\r\n"
#endif
#ifndef IS_LINE_END
#define IS_LINE_END(x) (return *(x)=='\r' || *(x)=='\0')	
#endif

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

typedef struct _rtsp_server_media_subsession{
	u32 id;
	void *parent_session;
	_list media_anchor;
	struct rtp_source *src;
	struct rtp_sink *sink;
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
	u32 subsession_cnt;
	u32 reference_cnt;
	struct rtsp_session session;
	//u32 time_stamp; // "Timestamp:[digit][.delay]"	
}rtsp_sm_session, *p_rtsp_sm_session;

typedef struct _rtsp_client_connection_session{
	TaskHandle_t task_id;
	void (*session_handle)(void *ctx);
	int client_socket;
	u16 client_port;
	u8 *client_ip;
}rtsp_cc_session, *p_rtsp_cc_session;

typedef struct _rtsp_client_media_subsession{
	u32 id;
	void *parent_session;
	_list media_anchor;
	struct rtp_source *src;
	struct rtp_sink *sink;	
}rtsp_cm_subsession, *p_rtsp_cm_subsession;

typedef struct _rtsp_client_media_session{
	_list media_entry;
	void *parent_client;
	u32 subsession_cnt;
}rtsp_cm_session, *p_rtsp_cm_session;

//struct to store basic configuration for create rtsp server
typedef struct _rtsp_adapter
{
	int max_subsession_nb;
	void *ext_adapter;
}rtsp_adapter;

struct rtsp_server{
	TaskHandle_t rtsp_task_id;
	void (*launch_handle)(void *ctx);
	void *ext_adapter; //pointer to external adapter
	u8 is_setup;
	u8 is_launched;
	u8 server_url[MAX_URL_LEN];
	int server_socket;
	u16 server_port;
	u8 *server_ip;
	struct rtsp_message message;
	u32 CSeq_now;
	rtsp_state state_now;
	rtsp_sm_session server_media;
	rtsp_cc_session client_connection;
};



extern int rtsp_req_OPTIONS_cb(void *ext_adapter);
extern int rtsp_req_DESCRIBE_cb(void *ext_adapter);
extern int rtsp_req_GET_PARAMETER_cb(void *ext_adapter);
extern int rtsp_req_SETUP_cb(void *ext_adapter);
extern int rtsp_req_PLAY_cb(void *ext_adapter);
extern int rtsp_req_TEARDOWN_cb(void *ext_adapter);
extern int rtsp_req_PAUSE_cb(void *ext_adapter);
extern int rtsp_req_UNDEFINED_cb(void *ext_adapter);

int rtsp_on_recv_OPTIONS();
int rtsp_on_recv_DESCRIBE();
int rtsp_on_recv_SETUP();
int rtsp_on_recv_PLAY();
int rtsp_on_recv_TEARDOWN();
int rtsp_on_recv_PAUSE();
int rtsp_on_recv_GET_PARAMETER();
int rtsp_on_recv_UNSUPPORTED();

int rtsp_server_media_subsession_add(rtsp_sm_session *session, rtsp_sm_subsession *subsession);
rtsp_sm_subsession * rtsp_sm_subsession_create(struct rtp_source *src, struct rtp_sink *sink, int max_sdp_size);
struct rtsp_server *rtsp_server_create(int max_subsession_nb);
int rtsp_server_setup(struct rtsp_server *server, const u8* server_url, int port);
int rtsp_server_launch(struct rtsp_server *server);
void rtsp_server_destroy(struct rtsp_server *server);


#endif