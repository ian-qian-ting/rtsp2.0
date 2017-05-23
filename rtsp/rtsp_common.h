#ifndef _RTSP_COMMON_H_
#define _RTSP_COMMON_H_

/***********************************INCLUDE************************************************/
#include "dlist.h"
#include "basic_types.h"
#include "osdep_service.h"

#include "sdp.h"

/*********************************************DEFINITIONS**************************************/

#define REQ_LINE_BUF_SIZE	64  //request line buf size
#define RES_LINE_BUF_SIZE	64  //response(status) line buf size 

/* rtsp request type list */
#define RTSP_REQ_UNDEFINED	0	
#define RTSP_REQ_OPTIONS 1
#define RTSP_REQ_DESCRIBE 2
#define RTSP_REQ_SETUP 3
#define RTSP_REQ_TEARDOWN 4
#define RTSP_REQ_PLAY 5
#define RTSP_REQ_PAUSE 6
#define RTSP_REQ_GET_PARAMETER 7
//#define RTSP_REQ_ANNOUNCE	8
//#define RTSP_REQ_RECORD		9
//#define RTSP_REQ_REDIRECT	10

/* rtsp supported response status list */
#define RTSP_RES_OK "RTSP/1.0 200 OK"
#define RTSP_RES_BAD "RTSP/1.0 400 Bad Request"
#define RTSP_RES_SNF "RTSP/1.0 454 Session Not Found"

/* rtsp header field particulars */

#define UNICAST_MODE	0	//set as default
#define MULTICAST_MODE	1
#define STR_UNICAST		"unicast"
#define STR_MULTICAST	"multicast"

#define TRANS_PROTO_UNKNOWN	0x00
#define TRANS_PROTO_RTP	0x01

#define TRANS_LOWER_PROTO_UNKNOWN	0x10
#define TRANS_LOWER_PROTO_UDP	0x11
#define TRANS_LOWER_PROTO_TCP	0x12


//accept&levels
#define ACCEPT_STR_SDP	"application/sdp"

#define PUBLIC_CMD_STR	"Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER"
#define ALLOW_CMD_STR	"Allow: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER"

/**************************************STRUCTURES********************************************************/

struct rtsp_session_info
{
	u32 session_id;
	u32 session_timeout;
	u8 *user;
	u8 *name;
	u8 *info;
	u32 version;
	u64 start_time;
	u64 end_time;
};

/* rtsp transport header field struct */
struct rtsp_transport
{
	u8 cast_mode; //unicast or multicast
	u8 proto; //transport protocol
	u8 lower_proto; //lower level transport protocol
	u8 ttl; //multicast ttl
	u16 port_even; //multicast RTP/RTCP port pair
	u16 port_odd; 
	u16 client_port_even; //unicast RTP/RTCP port pair for client
	u16 client_port_odd;
	u16 server_port_even; //unicast RTP/RTCP port pair for server 
	u16 server_port_odd;
	u32 ssrc; //only valid for unicast transmission
};

/* rtsp message specifics for use */
struct rtsp_message{
	int is_ready;	//set 1 when information has been successful updated
	u8 request_line[REQ_LINE_BUF_SIZE]; //for request
	u8 status_line[RES_LINE_BUF_SIZE]; //for response
	int method;
	//vital header field info
	u32 CSeq;
	u32 bandwidth; //measured in bits per sec
	u32 content_length; //must be set if any content
	struct rtsp_transport transport;
};

/************************************************DECLARATIONS************************************************/

#endif