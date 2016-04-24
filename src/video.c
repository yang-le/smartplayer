#include "config.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>
#include <libavutil/opt.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

#include <SDL2/SDL.h>

#include "debug.h"
#include "event.h"
#include "pktq.h"
#include "video.h"

static int video_stream_idx = -1;
static AVStream *video_stream = NULL;
static AVCodecContext *video_dec_ctx = NULL;
static AVFrame *frame_video = NULL;
static int video_frame_count = 0;
static PacketQueue video_queue = PACKET_QUEUE_INITIALIZER;

static int width = 0, height = 0;
static enum AVPixelFormat pix_fmt = AV_PIX_FMT_NONE;

static SDL_Texture* sdlTexture = NULL;
static SDL_Renderer* sdlRenderer = NULL;
static SDL_Rect sdlRect = {0, 0, 0, 0};
static SDL_TimerID videoTimerId = 0;

static AVFilterContext *buffersink_ctx;
static AVFilterContext *buffersrc_ctx;
static AVFilterGraph *filter_graph;

int init_video_filters(const char *filters_descr)
{
    char args[512];
    int ret = 0;
    AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVRational time_base = video_stream->time_base;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            width, height, pix_fmt, time_base.num, time_base.den,
            video_dec_ctx->sample_aspect_ratio.num, video_dec_ctx->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                       NULL, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr,
                                    &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

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

static Uint32 video_proc(Uint32 interval, void *opaque)
{  
	int delta = 0;
	int vt = 1000 / get_stream_fps(video_stream);

	// read from video_queue and paint
	AVPacket video_pkt;
	if (video_dequeue(&video_pkt)) {
		decode_video_packet(&video_pkt);

		int v_pts = get_video_pts();
		int a_pts = get_audio_pts();
		if ((v_pts > 0) && (a_pts > 0)) {
			delta = v_pts - a_pts;
		}
	}

	return (vt + delta > 0) ? (vt + delta) : 1;
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

            debug_info("video_frame n:%d coded_n:%d pts:%d\n",
                   video_frame_count++, frame_video->coded_picture_number,
                   get_video_pts());

		// IMPORTANT!!! fix pts !!!
		frame_video->pts = av_frame_get_best_effort_timestamp(frame_video);
		// IMPORTANT!!! fix pts !!!

		/* push the decoded frame into the filtergraph */
		ret = av_buffersrc_add_frame(buffersrc_ctx, frame_video);
		if (ret < 0) {
			av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
			return ret;
		}
		
		/* pull filtered frames from the filtergraph */
		while (1) {
			ret = av_buffersink_get_frame(buffersink_ctx, frame_video);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				break;
			if (ret < 0)
				return ret;
			
			SDL_UpdateYUVTexture(sdlTexture, &sdlRect,	
			frame_video->data[0], frame_video->linesize[0],  
			frame_video->data[1], frame_video->linesize[1],  
			frame_video->data[2], frame_video->linesize[2]);
			
			SDL_RenderClear(sdlRenderer);	 
			SDL_RenderCopy(sdlRenderer, sdlTexture,  NULL, &sdlRect);	  
			SDL_RenderPresent(sdlRenderer); 

			av_frame_unref(frame_video);
		}
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
		if (!frame_video) {
			fprintf(stderr, "Could not allocate frame\n");
			return AVERROR(ENOMEM);
		}
		
		video_stream = fmt_ctx->streams[video_stream_idx];
		video_dec_ctx = video_stream->codec;

		width = video_dec_ctx->width;
		height = video_dec_ctx->height;
		pix_fmt = video_dec_ctx->pix_fmt;
	}
	
	return ret;
}

int close_video_codec(void)
{
	av_frame_free(&frame_video);
	avcodec_close(video_dec_ctx);
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

/* inline */ void video_start()
{
	videoTimerId = SDL_AddTimer(1, video_proc, NULL);
}

/* inline */ void video_stop()
{
	SDL_RemoveTimer(videoTimerId);
}

/* inline */ int get_video_pts()
{
	int64_t pts = frame_video ? av_frame_get_best_effort_timestamp(frame_video) : AV_NOPTS_VALUE;
	if (pts == AV_NOPTS_VALUE) {
		return -1;
	} else {
		return av_rescale_q(pts, video_stream->time_base, (AVRational){1, 1000});
	}
}

