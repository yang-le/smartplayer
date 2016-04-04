#ifndef __VIDEO_H__
#define __VIDEO_H__

int sdl_video_init(void);

int open_video_codec(AVFormatContext *fmt_ctx);
int close_video_codec(void);
int decode_video_packet(AVPacket *pkt);

#endif
