#ifndef __SUBTITLE_H__
#define __SUBTITLE_H__

int open_subtitle_codec(AVFormatContext *fmt_ctx);
int close_subtitle_codec(void);
int decode_subtitle_packet(AVPacket *pkt);

int is_subtitle_packet(const AVPacket *pkt);
int subtitle_enqueue(const AVPacket *pkt);
int subtitle_dequeue(AVPacket *pkt);

void subtitle_start();
void subtitle_stop();

int get_subtitle_pts();

#endif
