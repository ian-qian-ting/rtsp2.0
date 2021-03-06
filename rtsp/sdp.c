#include "platform/platform_stdlib.h"
#include "basic_types.h"
#include "sdp.h"


void sdp_strcat(unsigned char *buf1, int size, unsigned char *buf2)
{
        int len1 = strlen(buf1);
        int len2 = strlen(buf2);
        int n = size - len1;
        strncat(buf1, buf2, (n < len2)? n:len2);
}

void sdp_fill_o_field(unsigned char *sdp_buf, int size, u8 *username, u32 session_id, u8 session_version, u8* nettype, u8* addrtype, u8* unicast_addr)
{
        unsigned char line[SDP_LINE_LEN] = {0};
        sprintf(line, "o=%s %x %d %s %s %d.%d.%d.%d" CRLF \
		            , (username)? username:"-", session_id, session_version, nettype, addrtype, unicast_addr[0], unicast_addr[1], unicast_addr[2], unicast_addr[3]);
        sdp_strcat(sdp_buf, size, line);
}

void sdp_fill_s_field(unsigned char *sdp_buf, int size, u8 * session_name)
{
        unsigned char line[SDP_LINE_LEN] = {0};
		sprintf(line, "i=%s" CRLF \
		            , (session_name)? session_name:" ");
		sdp_strcat(sdp_buf, size, line);
}

void sdp_fill_i_field(unsigned char *sdp_buf, int size, u8 * session_info)
{

}

void sdp_fill_u_field(unsigned char * sdp_buf, int size, u8 *uri)
{

}

void sdp_fill_c_field(unsigned char *sdp_buf, int size, u8 *nettype, u8 *addrtype, u8 *connection_addr, u8 ttl)
{
        unsigned char line[SDP_LINE_LEN] = {0};
		if(ttl == 0)
		{
			sprintf(line, "c=%s %s %d.%d.%d.%d" CRLF \
						, nettype, addrtype, connection_addr[0], connection_addr[1], connection_addr[2], connection_addr[3]);
		}else{
			sprintf(line, "c=%s %s %d.%d.%d.%d/%d" CRLF \
			            , nettype, addrtype, connection_addr, connection_addr[0], connection_addr[1], connection_addr[2], connection_addr[3], ttl);
		}
		sdp_strcat(sdp_buf, size, line);
}

void sdp_fill_b_field(unsigned char *sdp_buf, int size, int bwtype, int bw)
{
		unsigned char line[SDP_LINE_LEN] = {0};
		if(bwtype == SDP_BWTYPE_CT)
		{
			sprintf(line, "b=CT:%d" CRLF \
			, bw);
		}else if(bwtype == SDP_BWTYPE_AS)
			{
				sprintf(line, "b=AS:%d" CRLF \
							, bw);
			}else{
			    return;
			}
		sdp_strcat(sdp_buf, size, line);
}

void sdp_fill_t_field(unsigned char *sdp_buf, int size, u64 start_time, u64 end_time)
{
		unsigned char line[SDP_LINE_LEN] = {0};
		sprintf(line, "t=%d %d" CRLF \
		            , start_time, end_time);
		sdp_strcat(sdp_buf, size, line);			
}

void sdp_fill_m_field(unsigned char *sdp_buf, int size, int media_type, u16 port, int fmt)
{
		unsigned char line[SDP_LINE_LEN] = {0};
		switch(media_type)
		{
		    case(AVMEDIA_TYPE_VIDEO):
				sprintf(line, "m=video %d RTP/AVP %d" CRLF \
							, port, fmt);				
			    break;
			case(AVMEDIA_TYPE_AUDIO):
				sprintf(line, "m=audio %d RTP/AVP %d" CRLF \
							, port, fmt);				
			    break;
			case(AVMEDIA_TYPE_SUBTITLE):
			default:
			    printf("\n\runsupported media type");
			    return;
		}
		sdp_strcat(sdp_buf, size, line);					
}

void sdp_fill_a_string(unsigned char *sdp_buf, int size, u8 *string)
{
        unsigned char line[SDP_LINE_LEN] = {0};
		if((string == NULL) || strlen(string) > SDP_LINE_LEN)
			return;
		sprintf(line, "a=%s" CRLF \
		            , string);
		sdp_strcat(sdp_buf, size, line);        					
}
