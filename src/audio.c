#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>

#include <SDL2/SDL.h>

#include "debug.h"
#include "pktq.h"
#include "audio.h"

static int audio_stream_idx = -1;
static AVStream *audio_stream = NULL;
static AVCodecContext *audio_dec_ctx = NULL;
static AVFrame *frame_audio = NULL;
static int audio_frame_count = 0;
static PacketQueue audio_queue = PACKET_QUEUE_INITIALIZER;

static SDL_AudioFormat get_format(enum AVSampleFormat sample_fmt)
{
    int i = 0;
    SDL_AudioFormat fmt = 0;
    struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt; SDL_AudioFormat fmt_be, fmt_le;
    } sample_fmt_entries[] = {
        { AV_SAMPLE_FMT_U8,  AUDIO_U8, AUDIO_U8 },
        { AV_SAMPLE_FMT_S16, AUDIO_S16MSB, AUDIO_S16LSB },
        { AV_SAMPLE_FMT_S32, AUDIO_S32MSB, AUDIO_S32LSB },
        { AV_SAMPLE_FMT_FLT, AUDIO_F32MSB, AUDIO_F32LSB },
        
        { AV_SAMPLE_FMT_U8P,  AUDIO_U8, AUDIO_U8 },
        { AV_SAMPLE_FMT_S16P, AUDIO_S16MSB, AUDIO_S16LSB },
        { AV_SAMPLE_FMT_S32P, AUDIO_S32MSB, AUDIO_S32LSB },
        { AV_SAMPLE_FMT_FLTP, AUDIO_F32MSB, AUDIO_F32LSB }
    };

    for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt) {
            fmt = AV_NE(entry->fmt_be, entry->fmt_le);
            return fmt;
        }
    }

    fprintf(stderr,
            "sample format %s is not supported as output format\n",
            av_get_sample_fmt_name(sample_fmt));
    return fmt;
}

static void audio_proc(void *userdata, Uint8 *stream, int len)
{
	SDL_AudioSpec *spec = (SDL_AudioSpec *)userdata;

	unsigned int *size = &frame_audio->linesize[0];
	unsigned int *pos = &frame_audio->linesize[1];	// we use this to store pos info

	SDL_memset(stream, 0, len);

	while (len > 0){
		if (*pos >= *size) { //already send all our data, get more
			AVPacket audio_pkt;
			if (audio_dequeue(&audio_pkt)) {
				decode_audio_packet(&audio_pkt);
			} else {
				break;
			}
			*pos = 0;
		}

		int my_len = (*size > 0 ? *size : 0) - *pos;
		if (my_len > len) {
			my_len = len;
		}

		Uint8 *buf = frame_audio->extended_data[0];		
		SDL_MixAudioFormat(stream, &buf[*pos], AUDIO_S16SYS, len, SDL_MIX_MAXVOLUME);

		len -= my_len;
		stream += my_len;
		*pos += my_len;
	}
}

int decode_audio_packet(AVPacket *pkt)
{
    int ret = 0;
    int decoded = pkt->size;

    int _got_frame = 0, *got_frame = &_got_frame;

    if (pkt->stream_index == audio_stream_idx) {
        /* decode audio frame */
        ret = avcodec_decode_audio4(audio_dec_ctx, frame_audio, got_frame, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding audio frame (%s)\n", av_err2str(ret));
            return ret;
        }
        /* Some audio decoders decode only part of the packet, and have to be
         * called again with the remainder of the packet data.
         * Sample: fate-suite/lossless-audio/luckynight-partial.shn
         * Also, some decoders might over-read the packet. */
        decoded = FFMIN(ret, pkt->size);

        if (*got_frame) {
            size_t unpadded_linesize = frame_audio->nb_samples * av_get_bytes_per_sample(frame_audio->format);
		frame_audio->linesize[0] = unpadded_linesize;	// for test
#if 1
		debug_info("audio_frame n:%d nb_samples:%d pts:%d\n",
			audio_frame_count++, frame_audio->nb_samples,
			get_audio_pts());
#endif
            /* Write the raw audio data samples of the first plane. This works
             * fine for packed formats (e.g. AV_SAMPLE_FMT_S16). However,
             * most audio decoders output planar audio, which uses a separate
             * plane of audio samples for each channel (e.g. AV_SAMPLE_FMT_S16P).
             * In other words, this code will write only the first audio channel
             * in these cases.
             * You should use libswresample or libavfilter to convert the frame
             * to packed data. */
            //fwrite(frame_audio->extended_data[0], 1, unpadded_linesize, audio_file);
        }
    }

    return decoded;
}

int open_audio_codec(AVFormatContext *fmt_ctx)
{
	int ret = open_codec_context(&audio_stream_idx, fmt_ctx, AVMEDIA_TYPE_AUDIO);
	if (ret >= 0) {
		packet_queue_init(&audio_queue);

		frame_audio = av_frame_alloc();
		if (!frame_audio) {
			fprintf(stderr, "Could not allocate frame\n");
			return AVERROR(ENOMEM);
		}

		audio_stream = fmt_ctx->streams[audio_stream_idx];
		audio_dec_ctx = audio_stream->codec;
	}

	return ret;
}

int close_audio_codec(void)
{
	av_frame_free(&frame_audio);
	avcodec_close(audio_dec_ctx);
}

int sdl_audio_init(void)
{
	SDL_AudioSpec wanted_spec, spec;
	memset(&wanted_spec, 0, sizeof(wanted_spec));
	memset(&spec, 0, sizeof(spec));

	// Set audio settings from codec info
	wanted_spec.freq = audio_dec_ctx->sample_rate;
	wanted_spec.format = get_format(audio_dec_ctx->sample_fmt);
	wanted_spec.channels = audio_dec_ctx->channels;
	wanted_spec.samples = audio_dec_ctx->frame_size;
	wanted_spec.callback = audio_proc;
	wanted_spec.userdata = &spec;

	if (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
		fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
		return 1;
	}

	return 0;
}

/* inline */ int is_audio_packet(const AVPacket *pkt)
{
	return (pkt->stream_index == audio_stream_idx);
}

/* inline */ int audio_enqueue(const AVPacket *pkt)
{
	return packet_queue_put(&audio_queue, pkt);
}

/* inline */ int audio_dequeue(AVPacket *pkt)
{
	return packet_queue_get(&audio_queue, pkt);
}

/* inline */ void audio_start()
{
	SDL_PauseAudio(0);
}

/* inline */ void audio_stop()
{
	SDL_PauseAudio(1);
}

/* inline */ int get_audio_pts()
{
	int64_t pts = av_frame_get_best_effort_timestamp(frame_audio);
	if (pts == AV_NOPTS_VALUE) {
		return -1;
	} else {
		return av_rescale(pts, 1000, frame_audio->sample_rate);
	}
}

