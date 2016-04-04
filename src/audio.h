#ifndef __AUDIO_H__
#define __AUDIO_H__

int sdl_audio_init(void);

int open_audio_codec(AVFormatContext *fmt_ctx);
int close_audio_codec(void);
int decode_audio_packet(AVPacket *pkt);

#endif
