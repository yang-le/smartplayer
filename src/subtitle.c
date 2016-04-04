#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include "pktq.h"
#include "subtitle.h"

static int sub_stream_idx = -1;
static AVStream *sub_stream = NULL;
static AVCodecContext *sub_dec_ctx = NULL;
static AVSubtitle _frame_sub, *frame_sub = &_frame_sub;
static int sub_frame_count = 0;
static PacketQueue sub_queue = PACKET_QUEUE_INITIALIZER;

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
			fprintf(stderr, "sub frame goted!\n");
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

