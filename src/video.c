#include "config.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/timestamp.h>

#include <SDL2/SDL.h>

#include "debug.h"
#include "event.h"
#include "pktq.h"
#include "video.h"

static int video_stream_idx = -1;
static AVStream *video_stream = NULL;
static AVCodecContext *video_dec_ctx = NULL;
static AVFrame *frame_video = NULL, *frame_YUV = NULL;
static int video_frame_count = 0;
static PacketQueue video_queue = PACKET_QUEUE_INITIALIZER;

static int width = 0, height = 0;
static enum AVPixelFormat pix_fmt = AV_PIX_FMT_NONE;
static struct SwsContext *img_convert_ctx = NULL;

static SDL_Texture* sdlTexture = NULL;
static SDL_Renderer* sdlRenderer = NULL;
static SDL_Rect sdlRect = {0, 0, 0, 0};


static double get_stream_fps(const AVStream *s)
{
	AVRational a_fps = s->avg_frame_rate;
	if (a_fps.den && a_fps.num) {
		return av_q2d(a_fps);
	}

	AVRational r_fps = av_stream_get_r_frame_rate(s);
	if (r_fps.den && r_fps.num) {
		return av_q2d(r_fps);
	}

	return 25.0f;
}

static Uint32 fire_video_event(Uint32 interval, void *opaque)
{  
    SDL_Event event;  
    event.type = USR_VIDEO_EVENT;  
    SDL_PushEvent(&event);  
   
    return interval;  
} 

int decode_video_packet(AVPacket *pkt)
{
    int ret = 0;
    int decoded = pkt->size;

    int _got_frame = 0, *got_frame = &_got_frame;

    if (pkt->stream_index == video_stream_idx) {
        /* decode video frame */
        ret = avcodec_decode_video2(video_dec_ctx, frame_video, got_frame, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding video frame (%s)\n", av_err2str(ret));
            return ret;
        }

        if (*got_frame) {
            if (frame_video->width != width || frame_video->height != height ||
                frame_video->format != pix_fmt) {
                /* To handle this change, one could call av_image_alloc again and
                 * decode the following frames into another rawvideo file. */
                fprintf(stderr, "Error: Width, height and pixel format have to be "
                        "constant in a rawvideo file, but the width, height or "
                        "pixel format of the input video changed:\n"
                        "old: width = %d, height = %d, format = %s\n"
                        "new: width = %d, height = %d, format = %s\n",
                        width, height, av_get_pix_fmt_name(pix_fmt),
                        frame_video->width, frame_video->height,
                        av_get_pix_fmt_name(frame_video->format));
                return -1;
            }

            debug_info("video_frame n:%d coded_n:%d pts:%s\n",
                   video_frame_count++, frame_video->coded_picture_number,
                   av_ts2timestr(frame_video->pts, &video_dec_ctx->time_base));

		sws_scale(img_convert_ctx, (const uint8_t * const*)frame_video->data, frame_video->linesize, 0, height,	
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

    return decoded;
}

int open_video_codec(AVFormatContext *fmt_ctx)
{
	int ret = open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO);
	if (ret >= 0) {
		packet_queue_init(&video_queue);

		frame_video = av_frame_alloc();
		frame_YUV = av_frame_alloc();
		if (!frame_video || !frame_YUV) {
			fprintf(stderr, "Could not allocate frame\n");
			return AVERROR(ENOMEM);
		}
		
		video_stream = fmt_ctx->streams[video_stream_idx];
		video_dec_ctx = video_stream->codec;

		width = video_dec_ctx->width;
		height = video_dec_ctx->height;
		pix_fmt = video_dec_ctx->pix_fmt;

		unsigned char *out_buffer=(unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1));  
		if (!out_buffer) {
			fprintf(stderr, "Could not allocate yuv_buffer\n");
			return AVERROR(ENOMEM);
		}
		
		av_image_fill_arrays(frame_YUV->data, frame_YUV->linesize,out_buffer, AV_PIX_FMT_YUV420P,width, height,1);
		
		/* allocate img_convert_ctx */
		img_convert_ctx = sws_getContext(width, height, pix_fmt,
			width, height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
	}
	
	return ret;
}

int close_video_codec(void)
{
	av_frame_free(&frame_video);
	av_frame_free(&frame_YUV);

	avcodec_close(video_dec_ctx);
	sws_freeContext(img_convert_ctx);
}

int sdl_video_init(void)
{
	//SDL 2.0 Support for multiple windows	
	SDL_Window *screen = SDL_CreateWindow(PACKAGE_NAME,
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

	SDL_AddTimer(1000 / get_stream_fps(video_stream), fire_video_event, NULL);

	return 0;
}

/* inline */ int is_video_packet(const AVPacket *pkt)
{
	return (pkt->stream_index == video_stream_idx);
}

/* inline */ int video_enqueue(const AVPacket *pkt)
{
	return packet_queue_put(&video_queue, pkt);
}

/* inline */ int video_dequeue(AVPacket *pkt)
{
	return packet_queue_get(&video_queue, pkt);
}

