#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include <SDL2/SDL.h>

#include "pktq.h"
#include "subtitle.h"
#include "debug.h"

static int sub_stream_idx = -1;
static AVStream *sub_stream = NULL;
static AVCodecContext *sub_dec_ctx = NULL;
static AVSubtitle _frame_sub, *frame_sub = NULL;
static int sub_frame_count = 0;
static PacketQueue sub_queue = PACKET_QUEUE_INITIALIZER;
static SDL_TimerID subtitleTimerId = 0;

static void subtitle_dump(AVSubtitle *sub)
{
	printf("format = %d\n", sub->format);
	printf("start_display_time = %d\n", sub->start_display_time);
	printf("end_display_time = %d\n", sub->end_display_time);
	printf("num_rects = %d\n", sub->num_rects);
	printf("pts = %d\n", get_subtitle_pts());

	int i = 0;
	for (i = 0; i < sub->num_rects; ++i) {
		printf("rects[%d].x = %d\n", i, sub->rects[i]->x);
		printf("rects[%d].y = %d\n", i, sub->rects[i]->y);
		printf("rects[%d].w = %d\n", i, sub->rects[i]->w);
		printf("rects[%d].h = %d\n", i, sub->rects[i]->h);
		printf("rects[%d].nb_colors = %d\n", i, sub->rects[i]->x);
		printf("rects[%d].type = %d\n", i, sub->rects[i]->type);
		printf("rects[%d].text = %s\n", i, sub->rects[i]->text);
		printf("rects[%d].ass = %s\n", i, sub->rects[i]->ass);
	}
}

static Uint32 subtitle_proc(Uint32 interval, void *opaque)
{
	int ret = 0;

	if (frame_sub) {
		subtitle_dump(frame_sub);
	} else {
		frame_sub = &_frame_sub;
	}

	AVPacket sub_pkt;
	if (subtitle_dequeue(&sub_pkt)) {
		decode_subtitle_packet(&sub_pkt);

		int v_pts = get_video_pts();
		int a_pts = get_audio_pts();
		if (v_pts > 0) {
			ret = get_subtitle_pts() - v_pts;
		} else if (a_pts > 0) {
			ret = get_subtitle_pts() - a_pts;
		} else {	// both audio & video pts N.A.
			frame_sub = NULL;
			ret = 100;	// wait 100ms and try again	
		}
	} else {
		frame_sub = NULL;
		ret = 100;	// wait 100ms and try again
	}

	return (ret > 0) ? ret : 1;
} 

int decode_subtitle_packet(AVPacket *pkt)
{
	int ret = 0;
	int decoded = pkt->size;

	int _got_frame = 0, *got_frame = &_got_frame;

	if (pkt->stream_index == sub_stream_idx) {
		ret = avcodec_decode_subtitle2(sub_dec_ctx, frame_sub, got_frame, pkt);
		if (ret < 0) {
			fprintf(stderr, "Error decoding sub frame (%s)\n", av_err2str(ret));
			return ret;
		}

		if (*got_frame) {
			debug_info("got subtitle\n");
		}
	}

	return decoded;
}

int open_subtitle_codec(AVFormatContext *fmt_ctx)
{
	int ret = open_codec_context(&sub_stream_idx, fmt_ctx, AVMEDIA_TYPE_SUBTITLE);
	if (ret >= 0) {
		packet_queue_init(&sub_queue);
		
		sub_stream = fmt_ctx->streams[sub_stream_idx];
		sub_dec_ctx = sub_stream->codec;
	}

	return ret;
}

int close_subtitle_codec(void)
{
	avcodec_close(sub_dec_ctx);
}

/* inline */ int is_subtitle_packet(const AVPacket *pkt)
{
	return (pkt->stream_index == sub_stream_idx);
}

/* inline */ int subtitle_enqueue(const AVPacket *pkt)
{
	return packet_queue_put(&sub_queue, pkt);
}

/* inline */ int subtitle_dequeue(AVPacket *pkt)
{
	return packet_queue_get(&sub_queue, pkt);
}

/* inline */ void subtitle_start()
{
	subtitleTimerId = SDL_AddTimer(1, subtitle_proc, NULL);
}

/* inline */ void subtitle_stop()
{
	SDL_RemoveTimer(subtitleTimerId);
}

/* inline */ int get_subtitle_pts()
{
	int64_t pts = frame_sub->pts;
	if (pts == AV_NOPTS_VALUE) {
		return -1;
	} else {
		return av_rescale(pts, 1000, AV_TIME_BASE);
	}
}

