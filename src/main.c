#include "config.h"

#include <stdio.h>
#include <unistd.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
#include <libavutil/timestamp.h>
#include <libswscale/swscale.h>

#include <SDL2/SDL.h>

#include "debug.h"
#include "pktq.h"

#define ARG_REQ(x) #x":"
#define ARG_OPT(x) #x"::"

static AVFormatContext *fmt_ctx = NULL;
static int video_stream_idx = -1, audio_stream_idx = -1;
static AVStream *video_stream = NULL, *audio_stream = NULL;
static AVCodecContext *video_dec_ctx = NULL, *audio_dec_ctx = NULL;
static struct SwsContext *img_convert_ctx = NULL;

static int width = 0, height = 0;
static enum AVPixelFormat pix_fmt = AV_PIX_FMT_NONE;
static AVFrame *frame = NULL, *frame_YUV = NULL;
static AVPacket *pkt = NULL;
static int video_frame_count = 0;
static int audio_frame_count = 0;

static SDL_Texture* sdlTexture = NULL;
static SDL_Renderer* sdlRenderer = NULL;
static SDL_Rect sdlRect = {0, 0, 0, 0};

static PacketQueue video_queue = PACKET_QUEUE_INITIALIZER;
static PacketQueue audio_queue = PACKET_QUEUE_INITIALIZER;

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

static int sdl_init(int width, int height)
{
	SDL_Window *screen;

	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {	
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());   
		return 1;
	}	

	//SDL 2.0 Support for multiple windows	
	screen = SDL_CreateWindow(PACKAGE_NAME,
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,  width, height,  
		SDL_WINDOW_OPENGL);  

	if(!screen) {	 
		fprintf(stderr, "SDL: could not create window - exiting:%s\n",SDL_GetError());	  
		return 1;	
	}  

	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);	

	//IYUV: Y + U + V  (3 planes)  
	//YV12: Y + V + U  (3 planes)  
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,width,height);

	sdlRect.x = 0;
	sdlRect.y = 0;  
	sdlRect.w = width;  
	sdlRect.h = height;
	
	return 0;
}

static int open_codec_context(int *stream_idx,
                              AVFormatContext *fmt_ctx, enum AVMediaType type)
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

static int decode_packet(AVPacket *pkt/*, int *got_frame*/)
{
    int ret = 0;
    int decoded = pkt->size;

    int _got_frame = 0;
    int *got_frame = &_got_frame;

    if (pkt->stream_index == video_stream_idx) {
        /* decode video frame */
        ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding video frame (%s)\n", av_err2str(ret));
            return ret;
        }

        if (*got_frame) {
            if (frame->width != width || frame->height != height ||
                frame->format != pix_fmt) {
                /* To handle this change, one could call av_image_alloc again and
                 * decode the following frames into another rawvideo file. */
                fprintf(stderr, "Error: Width, height and pixel format have to be "
                        "constant in a rawvideo file, but the width, height or "
                        "pixel format of the input video changed:\n"
                        "old: width = %d, height = %d, format = %s\n"
                        "new: width = %d, height = %d, format = %s\n",
                        width, height, av_get_pix_fmt_name(pix_fmt),
                        frame->width, frame->height,
                        av_get_pix_fmt_name(frame->format));
                return -1;
            }

            debug_info("video_frame n:%d coded_n:%d pts:%s\n",
                   video_frame_count++, frame->coded_picture_number,
                   av_ts2timestr(frame->pts, &video_dec_ctx->time_base));

		sws_scale(img_convert_ctx, (const uint8_t * const*)frame->data, frame->linesize, 0, height,	
			frame_YUV->data, frame_YUV->linesize);

		SDL_UpdateYUVTexture(sdlTexture, &sdlRect,	
		frame_YUV->data[0], frame_YUV->linesize[0],  
		frame_YUV->data[1], frame_YUV->linesize[1],  
		frame_YUV->data[2], frame_YUV->linesize[2]);

		SDL_RenderClear(sdlRenderer);    
		SDL_RenderCopy(sdlRenderer, sdlTexture,  NULL, &sdlRect);	  
		SDL_RenderPresent(sdlRenderer);	
        }
    }

    if (pkt->stream_index == audio_stream_idx) {
        /* decode audio frame */
        ret = avcodec_decode_audio4(audio_dec_ctx, frame, got_frame, pkt);
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
            size_t unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(frame->format);
            debug_info("audio_frame n:%d nb_samples:%d pts:%s\n",
                   audio_frame_count++, frame->nb_samples,
                   av_ts2timestr(frame->pts, &audio_dec_ctx->time_base));

            /* Write the raw audio data samples of the first plane. This works
             * fine for packed formats (e.g. AV_SAMPLE_FMT_S16). However,
             * most audio decoders output planar audio, which uses a separate
             * plane of audio samples for each channel (e.g. AV_SAMPLE_FMT_S16P).
             * In other words, this code will write only the first audio channel
             * in these cases.
             * You should use libswresample or libavfilter to convert the frame
             * to packed data. */
            //fwrite(frame->extended_data[0], 1, unpadded_linesize, audio_dst_file);
            ret = SDL_QueueAudio(1, frame->extended_data[0], unpadded_linesize);
		if (ret < 0) {
			fprintf(stderr, "SDL_QueueAudio: %s\n", SDL_GetError());
			return ret;
		}
        }
    }

    return decoded;
}

int decode_thread(void *opaque)\
{
	/* read frames from the file */
	while (av_read_frame(fmt_ctx, pkt) >= 0) {
		if(pkt->stream_index == video_stream_idx) {
			packet_queue_put(&video_queue, pkt);
		} else if(pkt->stream_index == audio_stream_idx) {
			packet_queue_put(&audio_queue, pkt);
		} else {
			av_packet_unref(pkt);
		}
	}

	return 0;
}

//Refresh Event  
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1) 
#define SFM_AUDIO_EVENT  (SDL_USEREVENT + 2)  
#define SFM_BREAK_EVENT  (SDL_USEREVENT + 3)  

static Uint32 fire_audio_event(Uint32 interval, void *opaque)
{  
    SDL_Event event;  
    event.type = SFM_AUDIO_EVENT;  
    SDL_PushEvent(&event);  
   
    return interval;  
} 

static Uint32 fire_video_event(Uint32 interval, void *opaque)
{  
    SDL_Event event;  
    event.type = SFM_REFRESH_EVENT;  
    SDL_PushEvent(&event);  
   
    return interval;  
} 


static void sdl_event_loop()
{
	int thread_pause=0;

	for (;;) {	
		SDL_Event event;
		SDL_WaitEvent(&event);
		
		if(event.type==SFM_REFRESH_EVENT){
			if (!thread_pause) {
				// read from video_queue and paint
				packet_queue_get(&video_queue, pkt);
				decode_packet(pkt);
				av_packet_unref(pkt);
			}
		} else if(event.type==SFM_AUDIO_EVENT) {
			if (!thread_pause) {
				packet_queue_get(&audio_queue, pkt);
				decode_packet(pkt);
				av_packet_unref(pkt);
			}
		} else if(event.type==SDL_KEYDOWN) {	
			//Pause  
			if(event.key.keysym.sym==SDLK_SPACE) {
				thread_pause = !thread_pause;
				debug_info("%s\n", thread_pause ? "paused" : "playing");
			}
		} else if(event.type==SDL_QUIT) {  
			break;	
		} else if(event.type==SFM_BREAK_EVENT) {	
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

	if (open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
		video_stream = fmt_ctx->streams[video_stream_idx];
		video_dec_ctx = video_stream->codec;

		/* allocate img_convert_ctx */
		width = video_dec_ctx->width;
		height = video_dec_ctx->height;
		pix_fmt = video_dec_ctx->pix_fmt;
		img_convert_ctx = sws_getContext(width, height, pix_fmt,
			width, height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
	}

	if (sdl_init(width, height)) {
		fprintf(stderr, "SDL init failed!\n");
		ret = 1;
		goto end;
	}

	if (open_codec_context(&audio_stream_idx, fmt_ctx, AVMEDIA_TYPE_AUDIO) >= 0) {
		audio_stream = fmt_ctx->streams[audio_stream_idx];
		audio_dec_ctx = audio_stream->codec;

		SDL_AudioSpec wanted_spec, spec;
		memset(&wanted_spec, 0, sizeof(wanted_spec));
		memset(&spec, 0, sizeof(spec));

		// Set audio settings from codec info
		wanted_spec.freq = audio_dec_ctx->sample_rate;
		wanted_spec.format = AUDIO_S16SYS;
		wanted_spec.channels = audio_dec_ctx->channels;
		wanted_spec.samples = 1024;

		if (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
			fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
			ret = 1;
			goto end;
		}

		// start play sound
		SDL_PauseAudio(0);
	}

	/* dump input information to stderr */
	av_dump_format(fmt_ctx, 0, infile, 0);

	if (!audio_stream && !video_stream) {
		fprintf(stderr, "Could not find audio or video stream in the input, aborting\n");
		ret = 1;
		goto end;
	}

	frame = av_frame_alloc();
	frame_YUV = av_frame_alloc();
	if (!frame || !frame_YUV) {
		fprintf(stderr, "Could not allocate frame\n");
		ret = AVERROR(ENOMEM);
		goto end;
	}

	unsigned char *out_buffer=(unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1));  
	if (!out_buffer) {
		fprintf(stderr, "Could not allocate out_buffer\n");
		ret = AVERROR(ENOMEM);
		goto end;
	}

	av_image_fill_arrays(frame_YUV->data, frame_YUV->linesize,out_buffer, AV_PIX_FMT_YUV420P,width, height,1);

	/* initialize packet, let the demuxer fill it */
	pkt = av_packet_alloc();
	if (!pkt) {
		fprintf(stderr, "Could not allocate packet\n");
		ret = AVERROR(ENOMEM);
		goto end;
	}

	packet_queue_init(&video_queue);
	packet_queue_init(&audio_queue);

	if (video_stream)
		debug_info("Demuxing video from file '%s'\n", infile);
	if (audio_stream)
		debug_info("Demuxing audio from file '%s'\n", infile);

	SDL_AddTimer(40, fire_video_event, NULL);	// interval to be calc from fps
	SDL_AddTimer(50, fire_audio_event, NULL);	// interval to be calc from audio info
	SDL_CreateThread(decode_thread,NULL,NULL);

	sdl_event_loop();
	
	//debug_info("Demuxing succeeded.\n");

end:
	SDL_Quit();

	av_packet_free(&pkt);
	av_frame_free(&frame);
	av_frame_free(&frame_YUV);

	avcodec_close(audio_dec_ctx);
	avcodec_close(video_dec_ctx);
	sws_freeContext(img_convert_ctx);
	
	avformat_close_input(&fmt_ctx);

	return ret;
}

