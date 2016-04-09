#include "config.h"

#include <stdio.h>
#include <unistd.h>

#include <libavformat/avformat.h>
#include <SDL2/SDL.h>

#include "debug.h"
#include "audio.h"
#include "video.h"
#include "subtitle.h"
#include "event.h"

#define ARG_REQ(x) #x":"
#define ARG_OPT(x) #x"::"

static AVFormatContext *fmt_ctx = NULL;

static char* parse_args(int argc, char *argv[])
{
	int opt = 0;
	char *infile = NULL;
	
	while ((opt = getopt(argc, argv, ARG_REQ(i)"hv")) != -1) {
		switch (opt) {
		case 'i':
			infile = optarg;
			break;
		case 'h':
			printf("show help info here\n");
			break;
		case 'v':
			printf("show version info here\n");
			break;
		default:
			break;
		}
	}

	if (!infile) {
		infile = argv[optind];
	}

	return infile;
}

static int sdl_init(Uint32 flags)
{
	if(SDL_Init(flags | SDL_INIT_TIMER | SDL_INIT_EVENTS)) {	
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());   
		return 1;
	}	

	if (flags & SDL_INIT_VIDEO) {
		if (sdl_video_init()) {
			return 1;
		}
	}

	if (flags & SDL_INIT_AUDIO) {
		if (sdl_audio_init()) {
			return 1;
		}
	}

	return 0;
}

static int demux_thread(void *opaque)
{
	/* initialize packet, let the demuxer fill it */
	AVPacket *pkt = av_packet_alloc();
	if (!pkt) {
		fprintf(stderr, "Could not allocate packet\n");
		return AVERROR(ENOMEM);
	}

	/* read frames from the file */
	while (av_read_frame(fmt_ctx, pkt) >= 0) {
		if(is_video_packet(pkt)) {
			video_enqueue(pkt);
		} else if(is_audio_packet(pkt)) {
			audio_enqueue(pkt);
		} else if(is_subtitle_packet(pkt)) {
			subtitle_enqueue(pkt);
		} else {
			av_packet_unref(pkt);
		}
	}

	debug_info("demux done\n");

	av_packet_free(&pkt);
	return 0;
}

static void sdl_event_loop()
{
	int thread_pause=0;

	for (;;) {	
		SDL_Event event;
		SDL_WaitEvent(&event);
		
		if(event.type==SDL_KEYDOWN) {	
			//Pause  
			if(event.key.keysym.sym==SDLK_SPACE) {
				thread_pause = !thread_pause;
				debug_info("%s\n", thread_pause ? "paused" : "playing");
				if (thread_pause) {
					video_stop();
					audio_stop();
					subtitle_stop();
				} else {
					video_start();
					audio_start();
					subtitle_start();
				}
			}
		} else if(event.type==SDL_QUIT) {  
			break;	
		}
	}
}

int main(int argc, char *argv[])
{
	int ret = 0;
	char *infile = NULL;
	
	debug_info(PACKAGE_STRING"\n");

	infile = parse_args(argc, argv);

	if (!infile) {
		fprintf(stderr, "you must provide a input file\n");
		return 1;
	}

	debug_info("the input file is %s\n", infile);

	/* register all formats and codecs */
	av_register_all();
	avformat_network_init();
	avfilter_register_all();

	/* open input file, and allocate format context */
	if (avformat_open_input(&fmt_ctx, infile, NULL, NULL) < 0) {
		fprintf(stderr, "Could not open source file %s\n", infile);
		return 1;
	}

	/* retrieve stream information */
	if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		fprintf(stderr, "Could not find stream information\n");
		ret = 1;
		goto end;
	}

	int video_ok = open_video_codec(fmt_ctx);
	int audio_ok = open_audio_codec(fmt_ctx);
	int subtitle_ok = open_subtitle_codec(fmt_ctx);

	/* dump input information to stderr */
	av_dump_format(fmt_ctx, 0, infile, 0);

	if ((audio_ok < 0 ) && (video_ok < 0)) {
		fprintf(stderr, "Could not find audio or video stream in the input, aborting\n");
		ret = 1;
		goto end;
	}

	debug_info("Demuxing %s%s%sfrom file '%s'\n",
		(video_ok < 0) ? "" : "video ",
		(audio_ok < 0) ? "" : "audio ",
		(subtitle_ok < 0) ? "" : "subtitle ",
		infile);

	Uint32 sdl_flags = 0;
	sdl_flags |= (video_ok < 0) ? 0 : SDL_INIT_VIDEO;
	sdl_flags |= (video_ok < 0) ? 0 : SDL_INIT_AUDIO;
	
	if (sdl_init(sdl_flags)) {
		fprintf(stderr, "SDL init failed!\n");
		ret = 1;
		goto end;
	}

	char args[512];
	snprintf(args, sizeof(args), "subtitles=%s,crop=floor(in_w/2)*2:floor(in_h/2)*2", infile);
	init_video_filters(args);

	SDL_CreateThread(demux_thread,NULL,NULL);

	audio_start();
	video_start();
	subtitle_start();

	sdl_event_loop();
	
end:
	SDL_Quit();

	close_audio_codec();
	close_video_codec();
	close_subtitle_codec();
	
	avformat_close_input(&fmt_ctx);

	return ret;
}

int open_codec_context(int *stream_idx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    AVStream *st;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file\n",
                av_get_media_type_string(type));
        return ret;
    }

    stream_index = ret;
    st = fmt_ctx->streams[stream_index];

    /* find decoder for the stream */
    dec_ctx = st->codec;
    dec = avcodec_find_decoder(dec_ctx->codec_id);
    if (!dec) {
        fprintf(stderr, "Failed to find %s codec\n",
                av_get_media_type_string(type));
        return AVERROR(EINVAL);
    }

    /* Init the decoders */
    if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
        fprintf(stderr, "Failed to open %s codec\n",
                av_get_media_type_string(type));
        return ret;
    }
	
    *stream_idx = stream_index;
    return 0;
}

