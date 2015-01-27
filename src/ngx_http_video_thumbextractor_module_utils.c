/*
 * Copyright (C) 2011 Wandenberg Peixoto <wandenberg@gmail.com>
 *
 * This file is part of Nginx Video Thumb Extractor Module.
 *
 * Nginx Video Thumb Extractor Module is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Nginx Video Thumb Extractor Module is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Nginx Video Thumb Extractor Module.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * ngx_http_video_thumbextractor_module_utils.c
 *
 * Created:  Nov 22, 2011
 * Author:   Wandenberg Peixoto <wandenberg@gmail.com>
 *
 */
#include <ngx_http_video_thumbextractor_module_utils.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <jpeglib.h>

#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_BUFFER_SIZE 1024 * 8
#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_MEMORY_STEP 1024
#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_RGB         "RGB"

static uint32_t     ngx_http_video_thumbextractor_jpeg_compress(ngx_http_video_thumbextractor_loc_conf_t *cf, uint8_t * buffer, int linesize, int out_width, int out_height, caddr_t *out_buffer, size_t *out_len, size_t uncompressed_size, ngx_pool_t *temp_pool);
static void         ngx_http_video_thumbextractor_jpeg_memory_dest (j_compress_ptr cinfo, caddr_t *out_buf, size_t *out_size, size_t uncompressed_size, ngx_pool_t *temp_pool);

int setup_filters(ngx_http_video_thumbextractor_loc_conf_t *cf, AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx, int videoStream, AVFilterGraph **fg, AVFilterContext **buf_src_ctx, AVFilterContext **buf_sink_ctx, int width, int height, int second, ngx_log_t *log);
int filter_frame(AVFilterContext *buffersrc_ctx, AVFilterContext *buffersink_ctx, AVFrame *inFrame, AVFrame *outFrame, ngx_log_t *log);

static ngx_str_t *
ngx_http_video_thumbextractor_create_str(ngx_pool_t *pool, uint len)
{
    ngx_str_t *aux = (ngx_str_t *) ngx_pcalloc(pool, sizeof(ngx_str_t) + len + 1);
    if (aux != NULL) {
        aux->data = (u_char *) (aux + 1);
        aux->len = len;
        ngx_memset(aux->data, '\0', len + 1);
    }
    return aux;
}


int64_t ngx_http_video_thumbextractor_seek_data_from_file(void *opaque, int64_t offset, int whence)
{
    ngx_http_video_thumbextractor_file_info_t *info = (ngx_http_video_thumbextractor_file_info_t *) opaque;
    if (whence == AVSEEK_SIZE) {
        return info->size;
    }

    if ((whence == SEEK_SET) || (whence == SEEK_CUR) || (whence == SEEK_END)) {
        info->file.offset = lseek(info->file.fd, info->offset + offset, whence);
        return info->file.offset < 0 ? -1 : 0;
    }
    return -1;
}


int ngx_http_video_thumbextractor_read_data_from_file(void *opaque, uint8_t *buf, int buf_len)
{
    ngx_http_video_thumbextractor_file_info_t *info = (ngx_http_video_thumbextractor_file_info_t *) opaque;

    if ((info->offset > 0) && (info->file.offset < info->offset)) {
        info->file.offset = lseek(info->file.fd, info->offset, SEEK_SET);
        if (info->file.offset < 0) {
            return AVERROR(ngx_errno);
        }
    }

    ssize_t r = ngx_read_file(&info->file, buf, buf_len, info->file.offset);
    return (r == NGX_ERROR) ? AVERROR(ngx_errno) : r;
}


static int
ngx_http_video_thumbextractor_get_thumb(ngx_http_video_thumbextractor_loc_conf_t *cf, ngx_http_video_thumbextractor_file_info_t *info, int64_t second, ngx_uint_t width, ngx_uint_t height, caddr_t *out_buffer, size_t *out_len, ngx_pool_t *temp_pool, ngx_log_t *log)
{
    int              rc, ret, videoStream, frameFinished = 0, frameDecoded = 0;
    unsigned int     i;
    AVFormatContext *pFormatCtx = NULL;
    AVCodecContext  *pCodecCtx = NULL;
    AVCodec         *pCodec = NULL;
    AVFrame         *pFrame = NULL;
    AVPacket         packet;
    size_t           uncompressed_size;
    unsigned char   *bufferAVIO = NULL;
    AVIOContext     *pAVIOCtx = NULL;
    char            *filename = (char *) info->filename->data;
    ngx_file_info_t  fi;
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph   *filter_graph = NULL;

    ngx_memzero(&info->file, sizeof(ngx_file_t));
    info->file.name = *info->filename;
    info->file.log = log;

    rc = NGX_ERROR;

    // Open video file
    info->file.fd = ngx_open_file(filename, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
    if (info->file.fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Couldn't open file \"%V\"", info->filename);
        rc = NGX_HTTP_VIDEO_THUMBEXTRACTOR_FILE_NOT_FOUND;
        goto exit;
    }

    if (ngx_fd_info(info->file.fd, &fi) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: unable to stat file \"%V\"", info->filename);
        goto exit;
    }

    // Get file size
    info->size = (size_t) ngx_file_size(&fi) - info->offset;

    pFormatCtx = avformat_alloc_context();
    bufferAVIO = (unsigned char *) av_malloc(NGX_HTTP_VIDEO_THUMBEXTRACTOR_BUFFER_SIZE);
    if ((pFormatCtx == NULL) || (bufferAVIO == NULL)) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Couldn't alloc AVIO buffer");
        goto exit;
    }

    pAVIOCtx = avio_alloc_context(bufferAVIO, NGX_HTTP_VIDEO_THUMBEXTRACTOR_BUFFER_SIZE, 0, info, ngx_http_video_thumbextractor_read_data_from_file, NULL, ngx_http_video_thumbextractor_seek_data_from_file);
    if (pAVIOCtx == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Couldn't alloc AVIO context");
        goto exit;
    }

    pFormatCtx->pb = pAVIOCtx;

    // Open video file
    if ((ret = avformat_open_input(&pFormatCtx, filename, NULL, NULL)) != 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Couldn't open file %s, error: %d", filename, ret);
        rc = (ret == AVERROR(NGX_ENOENT)) ? NGX_HTTP_VIDEO_THUMBEXTRACTOR_FILE_NOT_FOUND : NGX_ERROR;
        goto exit;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Couldn't find stream information");
        goto exit;
    }

    if ((pFormatCtx->duration > 0) && ((((float_t) pFormatCtx->duration / AV_TIME_BASE) - second)) < 0.1) {
        ngx_log_error(NGX_LOG_WARN, log, 0, "video thumb extractor module: seconds greater than duration");
        rc = NGX_HTTP_VIDEO_THUMBEXTRACTOR_SECOND_NOT_FOUND;
        goto exit;
    }

    // Find the first video stream
    videoStream = -1;
    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            break;
        }
    }

    if (videoStream == -1) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Didn't find a video stream");
        goto exit;
    }

    // Get a pointer to the codec context for the video stream
    pCodecCtx = pFormatCtx->streams[videoStream]->codec;

    // Find the decoder for the video stream
    if ((pCodec = avcodec_find_decoder(pCodecCtx->codec_id)) == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Codec %d not found", pCodecCtx->codec_id);
        goto exit;
    }

    // Open codec
    if ((avcodec_open2(pCodecCtx, pCodec, NULL)) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Could not open codec");
        goto exit;
    }

    if (height == 0) {
        // keep original format
        width = pCodecCtx->width;
        height = pCodecCtx->height;
    } else if (width == 0) {
        // calculate width related with original aspect
        width = height * pCodecCtx->width / pCodecCtx->height;
    }

    if ((width < 16) || (height < 16)) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Very small size requested, %d x %d", width, height);
        goto exit;
    }

    if (setup_filters(cf, pFormatCtx, pCodecCtx, videoStream, &filter_graph, &buffersrc_ctx, &buffersink_ctx, width, height, second, log) < 0) {
        goto exit;
    }

    // Allocate video frame
    pFrame = av_frame_alloc();

    if (pFrame == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Could not alloc frame memory");
        goto exit;
    }

    // Determine required buffer size and allocate buffer
    uncompressed_size = avpicture_get_size(AV_PIX_FMT_RGB24, width, height) * sizeof(uint8_t);

    if ((av_seek_frame(pFormatCtx, -1, second * AV_TIME_BASE, cf->next_time ? 0 : AVSEEK_FLAG_BACKWARD)) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Seek to an invalid time");
        rc = NGX_HTTP_VIDEO_THUMBEXTRACTOR_SECOND_NOT_FOUND;
        goto exit;
    }

    int64_t second_on_stream_time_base = second * pFormatCtx->streams[videoStream]->time_base.den / pFormatCtx->streams[videoStream]->time_base.num;

    // Find the nearest frame
    rc = NGX_HTTP_VIDEO_THUMBEXTRACTOR_SECOND_NOT_FOUND;
    while (!frameFinished && av_read_frame(pFormatCtx, &packet) >= 0) {
        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream) {
            // Decode video frame
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
            // Did we get a video frame?
            if (frameFinished) {
                frameDecoded = 1;
                if (!cf->only_keyframe && (pFrame->pkt_pts < second_on_stream_time_base)) {
                    frameFinished = 0;
                }
            }
        }

        // Free the packet that was allocated by av_read_frame
        av_free_packet(&packet);
    }
    av_free_packet(&packet);

    if (frameDecoded) {
        rc = NGX_ERROR;

        if (filter_frame(buffersrc_ctx, buffersink_ctx, pFrame, pFrame, log) < 0) {
            goto exit;
        }

        // Compress to jpeg
        if (ngx_http_video_thumbextractor_jpeg_compress(cf, pFrame->data[0], pFrame->linesize[0], width, height, out_buffer, out_len, uncompressed_size, temp_pool) == 0) {
            rc = NGX_OK;
        }
    }

exit:

    if ((info->file.fd != NGX_INVALID_FILE) && (ngx_close_file(info->file.fd) == NGX_FILE_ERROR)) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Couldn't close file %s", filename);
        rc = NGX_ERROR;
    }

    /* destroy unneeded objects */

    // Free the YUV frame
    if (pFrame != NULL) av_frame_free(&pFrame);

    // Close the codec
    if (pCodecCtx != NULL) avcodec_close(pCodecCtx);

    // Close the video file
    if (pFormatCtx != NULL) avformat_close_input(&pFormatCtx);

    // Free AVIO context
    if (pAVIOCtx != NULL) {
        if (pAVIOCtx->buffer != NULL) av_freep(&pAVIOCtx->buffer);
        av_freep(&pAVIOCtx);
    }

    if (filter_graph != NULL) avfilter_graph_free(&filter_graph);

    return rc;
}


static void
ngx_http_video_thumbextractor_init_libraries(void)
{
    // Register all formats and codecs
    av_register_all();
    avfilter_register_all();
    av_log_set_level(AV_LOG_ERROR);
}


static uint32_t
ngx_http_video_thumbextractor_jpeg_compress(ngx_http_video_thumbextractor_loc_conf_t *cf, uint8_t * buffer, int linesize, int out_width, int out_height, caddr_t *out_buffer, size_t *out_len, size_t uncompressed_size, ngx_pool_t *temp_pool)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];

    if ( !buffer ) return 1;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    ngx_http_video_thumbextractor_jpeg_memory_dest(&cinfo, out_buffer, out_len, uncompressed_size, temp_pool);

    cinfo.image_width = out_width;
    cinfo.image_height = out_height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    /* Important: Header info must be set AFTER jpeg_set_defaults() */
    cinfo.write_JFIF_header = TRUE;
    cinfo.JFIF_major_version = 1;
    cinfo.JFIF_minor_version = 2;
    cinfo.density_unit = 1; /* 0=unknown, 1=dpi, 2=dpcm */
    cinfo.X_density = cf->jpeg_dpi;
    cinfo.Y_density = cf->jpeg_dpi;
    cinfo.write_Adobe_marker = TRUE;

    jpeg_set_quality(&cinfo, cf->jpeg_quality, cf->jpeg_baseline);
    cinfo.optimize_coding = cf->jpeg_optimize;
    cinfo.smoothing_factor = cf->jpeg_smooth;

    if ( cf->jpeg_progressive_mode ) {
        jpeg_simple_progression(&cinfo);
    }

    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &buffer[cinfo.next_scanline * linesize];
        (void)jpeg_write_scanlines(&cinfo, row_pointer,1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    return 0;
}


typedef struct {
    struct jpeg_destination_mgr  pub; /* public fields */

    unsigned char              **buf;
    size_t                      *size;
    size_t                       uncompressed_size;
    ngx_pool_t                  *pool;
} ngx_http_video_thumbextractor_jpeg_destination_mgr;


static void ngx_http_video_thumbextractor_init_destination (j_compress_ptr cinfo)
{
    ngx_http_video_thumbextractor_jpeg_destination_mgr * dest = (ngx_http_video_thumbextractor_jpeg_destination_mgr *) cinfo->dest;

    *(dest->buf) = ngx_palloc(dest->pool, dest->uncompressed_size);
    *(dest->size) = dest->uncompressed_size;
    dest->pub.next_output_byte = *(dest->buf);
    dest->pub.free_in_buffer = dest->uncompressed_size;
}


static boolean ngx_http_video_thumbextractor_empty_output_buffer (j_compress_ptr cinfo)
{
    ngx_http_video_thumbextractor_jpeg_destination_mgr *dest = (ngx_http_video_thumbextractor_jpeg_destination_mgr *) cinfo->dest;
    unsigned char                                      *ret;

    ret = ngx_palloc(dest->pool, *(dest->size) + NGX_HTTP_VIDEO_THUMBEXTRACTOR_MEMORY_STEP);
    ngx_memcpy(ret, *(dest->buf), *(dest->size));

    *(dest->buf) = ret;
    (*dest->size) += NGX_HTTP_VIDEO_THUMBEXTRACTOR_MEMORY_STEP;

    dest->pub.next_output_byte = *(dest->buf) + *(dest->size) - NGX_HTTP_VIDEO_THUMBEXTRACTOR_MEMORY_STEP ;
    dest->pub.free_in_buffer = NGX_HTTP_VIDEO_THUMBEXTRACTOR_MEMORY_STEP;

    return TRUE;
}


static void ngx_http_video_thumbextractor_term_destination (j_compress_ptr cinfo)
{
    ngx_http_video_thumbextractor_jpeg_destination_mgr *dest = (ngx_http_video_thumbextractor_jpeg_destination_mgr *) cinfo->dest;
    *(dest->size)-=dest->pub.free_in_buffer;
}


static void
ngx_http_video_thumbextractor_jpeg_memory_dest (j_compress_ptr cinfo, caddr_t *out_buf, size_t *out_size, size_t uncompressed_size, ngx_pool_t *temp_pool)
{
    ngx_http_video_thumbextractor_jpeg_destination_mgr *dest;

    if (cinfo->dest == NULL) {
        cinfo->dest = (struct jpeg_destination_mgr *)(*cinfo->mem->alloc_small)((j_common_ptr) cinfo, JPOOL_PERMANENT, sizeof(ngx_http_video_thumbextractor_jpeg_destination_mgr));
    }

    dest = (ngx_http_video_thumbextractor_jpeg_destination_mgr *) cinfo->dest;
    dest->pub.init_destination = ngx_http_video_thumbextractor_init_destination;
    dest->pub.empty_output_buffer = ngx_http_video_thumbextractor_empty_output_buffer;
    dest->pub.term_destination = ngx_http_video_thumbextractor_term_destination;
    dest->buf = (unsigned char **)out_buf;
    dest->size = out_size;
    dest->uncompressed_size = uncompressed_size;
    dest->pool = temp_pool;
}


int setup_filters(ngx_http_video_thumbextractor_loc_conf_t *cf, AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx, int videoStream, AVFilterGraph **fg, AVFilterContext **buffersrc_ctx, AVFilterContext **buffersink_ctx, int width, int height, int second, ngx_log_t *log)
{
    AVFilterGraph   *filter_graph;

    AVFilterContext *scale_ctx;
    AVFilterContext *crop_ctx;
    AVFilterContext *format_ctx;

    int              rc = 0;
    char             args[512];

    unsigned int     needs_crop = 0;
    float            scale = 0.0, new_scale = 0.0, scale_sws = 0.0, scale_w = 0.0, scale_h = 0.0;
    int              sws_width = 0, sws_height = 0;

    scale     = (float) pCodecCtx->width / pCodecCtx->height;
    new_scale = (float) width / height;

    sws_width = width;
    sws_height = height;

    if (scale != new_scale) {
        scale_w = (float) width / pCodecCtx->width;
        scale_h = (float) height / pCodecCtx->height;
        scale_sws = (scale_w > scale_h) ? scale_w : scale_h;

        sws_width = pCodecCtx->width * scale_sws + 0.5;
        sws_height = pCodecCtx->height * scale_sws + 0.5;

        needs_crop = 1;
    }

    // create filters to scale and crop the selected frame
    if ((*fg = filter_graph = avfilter_graph_alloc()) == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: unable to create filter graph: out of memory");
        return NGX_ERROR;
    }

    AVRational time_base = pFormatCtx->streams[videoStream]->time_base;
    snprintf(args, sizeof(args),
        "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
        pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
        time_base.num, time_base.den,
        pCodecCtx->sample_aspect_ratio.num, pCodecCtx->sample_aspect_ratio.den);

    if (avfilter_graph_create_filter(buffersrc_ctx, avfilter_get_by_name("buffer"), NULL, args, NULL, filter_graph) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Cannot create buffer source");
        return NGX_ERROR;
    }

    snprintf(args, sizeof(args), "%d:%d:flags=bicubic", sws_width, sws_height);
    if (avfilter_graph_create_filter(&scale_ctx, avfilter_get_by_name("scale"), NULL, args, NULL, filter_graph) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: error initializing scale filter");
        return NGX_ERROR;
    }

    if (needs_crop) {
        snprintf(args, sizeof(args), "%d:%d", width, height);
        if (avfilter_graph_create_filter(&crop_ctx, avfilter_get_by_name("crop"), NULL, args, NULL, filter_graph) < 0) {
            ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: error initializing crop filter");
            return NGX_ERROR;
        }
    }

    if (avfilter_graph_create_filter(&format_ctx, avfilter_get_by_name("format"), NULL, "pix_fmts=rgb24", NULL, filter_graph) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: error initializing format filter");
        return NGX_ERROR;
    }

    /* buffer video sink: to terminate the filter chain. */
    if (avfilter_graph_create_filter(buffersink_ctx, avfilter_get_by_name("buffersink"), NULL, NULL, NULL, filter_graph) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Cannot create buffer sink");
        return NGX_ERROR;
    }

    // connect inputs and outputs
    rc = avfilter_link(*buffersrc_ctx, 0, scale_ctx, 0);

    if (needs_crop) {
        if (rc >= 0) rc = avfilter_link(scale_ctx, 0, crop_ctx, 0);
        if (rc >= 0) rc = avfilter_link(crop_ctx, 0, format_ctx, 0);
    } else {
        if (rc >= 0) rc = avfilter_link(scale_ctx, 0, format_ctx, 0);
    }

    if (rc >= 0) rc = avfilter_link(format_ctx, 0, *buffersink_ctx, 0);

    if (rc < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: error connecting filters");
        return NGX_ERROR;
    }

    if (avfilter_graph_config(filter_graph, NULL) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: error configuring the filter graph");
        return NGX_ERROR;
    }

    return NGX_OK;
}


int filter_frame(AVFilterContext *buffersrc_ctx, AVFilterContext *buffersink_ctx, AVFrame *inFrame, AVFrame *outFrame, ngx_log_t *log)
{
    int rc = NGX_OK;

    if (av_buffersrc_add_frame_flags(buffersrc_ctx, inFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Error while feeding the filtergraph");
        return NGX_ERROR;
    }

    if ((rc = av_buffersink_get_frame(buffersink_ctx, outFrame)) < 0) {
        if (rc != AVERROR(EAGAIN)) {
            ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Error while getting the filtergraph result frame");
        }
    }

    return rc;
}
