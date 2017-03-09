
#include "rtcp_api.h"

/*
* Return length parsed, -1 on error.
*/
static int rtcp_parse_control(char *buf, int len)
{
  rtcp_t *r;         /* RTCP header */
  int i;

  r = (rtcp_t *)buf;
  if (r->common.version == RTP_VERSION) {
    printf("\n");
    while (len > 0) {
      len -= (ntohs(r->common.length) + 1) << 2;
      if (len < 0) {
        /* something wrong with packet format */
        printf("Illegal RTCP packet length %d words.\n",
	       ntohs(r->common.length));
        return -1;
      }

      switch (r->common.pt) {
      case RTCP_SR:
        printf(" (SR ssrc=0x%lx p=%d count=%d len=%d\n", 
          (unsigned long)ntohl(r->r.rr.ssrc),
          r->common.p, r->common.count,
	      ntohs(r->common.length));
        printf("ntp=%lu.%lu ts=%lu psent=%lu osent=%lu\n",
          (unsigned long)ntohl(r->r.sr.ntp_sec),
          (unsigned long)ntohl(r->r.sr.ntp_frac),
          (unsigned long)ntohl(r->r.sr.rtp_ts),
          (unsigned long)ntohl(r->r.sr.psent),
          (unsigned long)ntohl(r->r.sr.osent));
        for (i = 0; i < r->common.count; i++) {
          printf("  (ssrc=%0lx fraction=%g lost=%lu last_seq=%lu jit=%lu lsr=%lu dlsr=%lu)\n",
           (unsigned long)ntohl(r->r.sr.rr[i].ssrc),
           r->r.sr.rr[i].fraction / 256.,
           (unsigned long)ntohl(r->r.sr.rr[i].lost), /* XXX I'm pretty sure this is wrong */
           (unsigned long)ntohl(r->r.sr.rr[i].last_seq),
           (unsigned long)ntohl(r->r.sr.rr[i].jitter),
           (unsigned long)ntohl(r->r.sr.rr[i].lsr),
           (unsigned long)ntohl(r->r.sr.rr[i].dlsr));
        }
        printf(" )\n"); 
        break;

      case RTCP_RR:
        printf(" (RR ssrc=0x%lx p=%d count=%d len=%d\n", 
          (unsigned long)ntohl(r->r.rr.ssrc), r->common.p, r->common.count,
	      ntohs(r->common.length));
        for (i = 0; i < r->common.count; i++) {
          printf("(ssrc=%0lx fraction=%g lost=%lu last_seq=%lu jit=%lu lsr=%lu dlsr=%lu)\n",
            (unsigned long)ntohl(r->r.rr.rr[i].ssrc),
            r->r.rr.rr[i].fraction / 256.,
            (unsigned long)ntohl(r->r.rr.rr[i].lost),
            (unsigned long)ntohl(r->r.rr.rr[i].last_seq),
            (unsigned long)ntohl(r->r.rr.rr[i].jitter),
            (unsigned long)ntohl(r->r.rr.rr[i].lsr),
            (unsigned long)ntohl(r->r.rr.rr[i].dlsr));
        }
        printf(" )\n"); 
        break;

      case RTCP_SDES:
        printf(" (SDES p=%d count=%d len=%d\n", 
          r->common.p, r->common.count, ntohs(r->common.length));
        buf = (char *)&r->r.sdes;
        for (i = 0; i < r->common.count; i++) {
          int remaining = (ntohs(r->common.length) << 2) -
	    (buf - (char *)&r->r.sdes);

          printf("  (src=0x%lx ", 
            (unsigned long)ntohl(((struct rtcp_sdes *)buf)->src));
          if (remaining > 0) {
            buf = rtp_read_sdes(buf, 
              (ntohs(r->common.length) << 2) - (buf - (char *)&r->r.sdes));
            if (!buf) return -1;
          }
          else {
            fprintf(stderr, "Missing at least %d bytes.\n", -remaining);
            return -1;
          }
          printf(")\n"); 
        }
        printf(" )\n"); 
        break;

      case RTCP_BYE:
        printf(" (BYE p=%d count=%d len=%d\n", 
          r->common.p, r->common.count, ntohs(r->common.length));
        for (i = 0; i < r->common.count; i++) {
          printf("ssrc[%d]=%0lx ", i, 
            (unsigned long)ntohl(r->r.bye.src[i]));
        }
        if (ntohs(r->common.length) > r->common.count) {
          buf = (char *)&r->r.bye.src[r->common.count];
          printf("reason=\"%*.*s\"", *buf, *buf, buf+1); 
        }
        printf(")\n");
        break;

      /* invalid type */
      default:
        printf("(? pt=%d src=0x%lx)\n", r->common.pt, 
          (unsigned long)ntohl(r->r.sdes.src));
      break;
      }
      r = (rtcp_t *)((u_int32 *)r + ntohs(r->common.length) + 1);
    }
  }
  else {
    printf("invalid version %d\n", r->common.version);
  }
  return len;
} /* rtcp_parse_control */