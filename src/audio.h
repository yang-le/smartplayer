#ifndef __AUDIO_H__
#define __AUDIO_H__

int sdl_audio_init(void);

int open_audio_codec(AVFormatContext *fmt_ctx);
int close_audio_codec(void);
int decode_audio_packet(AVPacket *pkt);

int is_audio_packet(const AVPacket *pkt);
int audio_enqueue(const AVPacket *pkt);
int audio_dequeue(AVPacket *pkt);

void audio_start();
void audio_stop();

int get_audio_pts();

#endif
