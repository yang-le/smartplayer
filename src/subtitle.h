#ifndef __SUBTITLE_H__
#define __SUBTITLE_H__

int open_subtitle_codec(AVFormatContext *fmt_ctx);
int close_subtitle_codec(void);
int decode_subtitle_packet(AVPacket *pkt);

#endif
