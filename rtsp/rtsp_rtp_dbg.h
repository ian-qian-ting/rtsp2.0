#ifndef _RTSP_RTP_DBG_H_
#define _RTSP_RTP_DBG_H_

#include "basic_types.h"
#include "platform/platform_stdlib.h"

/* error code */

#define DEBUG_LEVEL_0   0
#define DEBUG_LEVEL_1	1
#define DEBUG_LEVEL_2   2

#define DEBUG_RTSP_LEVEL DEBUG_LEVEL_2
#define DEBUG_RTP_LEVEL DEBUG_LEVEL_2
#define DEBUG_RTCP_LEVEL DEBUG_LEVEL_2

/*********************************************for rtsp level debug*****************************************/



#if (DEBUG_RTSP_LEVEL == DEBUG_LEVEL_1)
#define RTSP_INFO(fmt, args...)
#define RTSP_WARN(fmt, args...)		printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#define RTSP_ERROR(fmt, args...)    printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#elif (DEBUG_RTSP_LEVEL == DEBUG_LEVEL_2)
#define RTSP_INFO(fmt, args...)		printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#define RTSP_WARN(fmt, args...)		printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#define RTSP_ERROR(fmt, args...)    printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#else
#define RTSP_INFO(fmt, args...)
#define RTSP_WARN(fmt, args...)
#define RTSP_ERROR(fmt, args...)    printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#endif


/*********************************************for rtp level debug******************************************/

#if (DEBUG_RTP_LEVEL == DEBUG_LEVEL_1)
#define RTP_INFO(fmt, args...)
#define RTP_WARN(fmt, args...)		printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#define RTP_ERROR(fmt, args...)    printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#elif (DEBUG_RTSP_LEVEL == DEBUG_LEVEL_2)
#define RTP_INFO(fmt, args...)		printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#define RTP_WARN(fmt, args...)		printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#define RTP_ERROR(fmt, args...)    printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#else
#define RTP_INFO(fmt, args...)
#define RTP_WARN(fmt, args...)
#define RTP_ERROR(fmt, args...)    printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#endif

/*********************************************for rtcp level debug*****************************************/

#if (DEBUG_RTCP_LEVEL == DEBUG_LEVEL_1)
#define RTCP_INFO(fmt, args...)
#define RTCP_WARN(fmt, args...)		printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#define RTCP_ERROR(fmt, args...)    printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#elif (DEBUG_RTSP_LEVEL == DEBUG_LEVEL_2)
#define RTCP_INFO(fmt, args...)		printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#define RTCP_WARN(fmt, args...)		printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#define RTCP_ERROR(fmt, args...)    printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#else
#define RTCP_INFO(fmt, args...)
#define RTCP_WARN(fmt, args...)
#define RTCP_ERROR(fmt, args...)    printf("\n\r%s: " fmt, __FUNCTION__, ## args)
#endif

#endif