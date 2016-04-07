#ifndef __VIDEO_H__
#define __VIDEO_H__

int sdl_video_init(void);

int open_video_codec(AVFormatContext *fmt_ctx);
int close_video_codec(void);
int decode_video_packet(AVPacket *pkt);

int is_video_packet(const AVPacket *pkt);
int video_enqueue(const AVPacket *pkt);
int video_dequeue(AVPacket *pkt);

void video_start();
void video_stop();

int get_video_pts();

#endif
