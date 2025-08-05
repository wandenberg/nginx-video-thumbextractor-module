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
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/display.h>
#include <jpeglib.h>

#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_BUFFER_SIZE 1024 * 8
#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_MEMORY_STEP 1024
#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_RGB         "RGB"

static uint32_t     ngx_http_video_thumbextractor_jpeg_compress(ngx_http_video_thumbextractor_loc_conf_t *cf, uint8_t * buffer, int linesize, int out_width, int out_height, caddr_t *out_buffer, size_t *out_len, size_t uncompressed_size, ngx_pool_t *temp_pool);
static void         ngx_http_video_thumbextractor_jpeg_memory_dest (j_compress_ptr cinfo, caddr_t *out_buf, size_t *out_size, size_t uncompressed_size, ngx_pool_t *temp_pool);

int setup_parameters(ngx_http_video_thumbextractor_loc_conf_t *cf, ngx_http_video_thumbextractor_thumb_ctx_t *ctx, AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx);
int setup_filters(ngx_http_video_thumbextractor_thumb_ctx_t *ctx, AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx, int videoStream, AVFilterGraph **fg, AVFilterContext **buf_src_ctx, AVFilterContext **buf_sink_ctx, ngx_log_t *log);
int filter_frame(AVFilterContext *buffersrc_ctx, AVFilterContext *buffersink_ctx, AVFrame *inFrame, AVFrame *outFrame, ngx_log_t *log);
int get_frame(ngx_http_video_thumbextractor_loc_conf_t *cf, AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx, AVFrame *pFrame, int videoStream, int64_t second, ngx_log_t *log);


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
ngx_http_video_thumbextractor_get_thumb(ngx_http_video_thumbextractor_loc_conf_t *cf, ngx_http_video_thumbextractor_thumb_ctx_t *ctx, caddr_t *out_buffer, size_t *out_len, ngx_pool_t *temp_pool, ngx_log_t *log)
{
    ngx_http_video_thumbextractor_file_info_t *info = &ctx->file_info;
    int              rc, ret, videoStream;
    AVFormatContext *pFormatCtx = NULL;
    AVCodecContext  *pCodecCtx = NULL;
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(59, 16, 100)
    AVCodec         *pCodec = NULL;
#else
    const AVCodec   *pCodec = NULL;
#endif
    AVFrame         *pFrame = NULL;
    size_t           uncompressed_size;
    unsigned char   *bufferAVIO = NULL;
    AVIOContext     *pAVIOCtx = NULL;
    char            *filename = (char *) ctx->filename.data;
    ngx_file_info_t  fi;
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph   *filter_graph = NULL;
    int              need_flush = 0;
    int64_t          second = ctx->second;
    char             value[10];

    ngx_memzero(&info->file, sizeof(ngx_file_t));
    info->file.name = ctx->filename;
    info->file.log = log;

    rc = NGX_ERROR;

    // Open video file
    info->file.fd = ngx_open_file(filename, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
    if (info->file.fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Couldn't open file \"%V\"", &ctx->filename);
        rc = NGX_HTTP_VIDEO_THUMBEXTRACTOR_FILE_NOT_FOUND;
        goto exit;
    }

    if (ngx_fd_info(info->file.fd, &fi) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: unable to stat file \"%V\"", &ctx->filename);
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
    videoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
    if (videoStream == -1) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Didn't find a video stream");
        goto exit;
    }

    // Get a pointer to the codec context for the video stream
    pCodecCtx = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);

    AVDictionary *dict = NULL;
    ngx_sprintf((u_char *) value, "%V%Z", &cf->threads);
    av_dict_set(&dict, "threads", value, 0);

    // Open codec
    if ((avcodec_open2(pCodecCtx, pCodec, &dict)) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Could not open codec");
        goto exit;
    }

    setup_parameters(cf, ctx, pFormatCtx, pCodecCtx);

    if (setup_filters(ctx, pFormatCtx, pCodecCtx, videoStream, &filter_graph, &buffersrc_ctx, &buffersink_ctx, log) < 0) {
        goto exit;
    }

    // Allocate video frame
    pFrame = av_frame_alloc();

    if (pFrame == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Could not alloc frame memory");
        goto exit;
    }

    u_char *vtt_buf, *vtt_buf_cur;
    size_t vtt_buf_size = (pFormatCtx->duration / AV_TIME_BASE / ctx->tile_sample_interval) * (50+100); // 50 symbols for timeframe, 100 symbols for storyboard link and coordinates
    vtt_buf = ngx_palloc(temp_pool, vtt_buf_size);
    if (vtt_buf == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Could not alloc vtt memory");
        goto exit;
    }

    vtt_buf_cur = vtt_buf;
    vtt_buf_cur = ngx_snprintf(vtt_buf_cur, vtt_buf_size - (vtt_buf_cur - vtt_buf), "WEBVTT\n");

    int64_t finishCueSecond;
    int64_t startCueHours, startCueMinutes, startCueSeconds;
    int64_t finishCueHours, finishCueMinutes, finishCueSeconds;
    int64_t cur_tile_row = 1, cur_tile_col = 1;


    while ((rc = get_frame(cf, pFormatCtx, pCodecCtx, pFrame, videoStream, second, log)) == 0) {
        if (pFrame->pict_type == AV_PICTURE_TYPE_NONE) {
            need_flush = 1;
            break;
        }

        if (filter_frame(buffersrc_ctx, buffersink_ctx, pFrame, pFrame, log) == AVERROR(EAGAIN)) {
            finishCueSecond = second + ctx->tile_sample_interval;
            startCueHours = second / 3600;
            startCueMinutes = (second - startCueHours * 3600) / 60;
            startCueSeconds = second - startCueHours * 3600 - startCueMinutes * 60;
            finishCueHours = finishCueSecond / 3600;
            finishCueMinutes = (finishCueSecond - finishCueHours * 3600) / 60;
            finishCueSeconds = finishCueSecond - finishCueHours * 3600 - finishCueMinutes * 60;

            vtt_buf_cur = ngx_snprintf(vtt_buf_cur, vtt_buf_size - (vtt_buf_cur - vtt_buf), "\n");
            vtt_buf_cur = ngx_snprintf(vtt_buf_cur, vtt_buf_size - (vtt_buf_cur - vtt_buf), "%02d:%02d:%02d.000 --> %02d:%02d:%02d.000\n", startCueHours, startCueMinutes, startCueSeconds, finishCueHours, finishCueMinutes, finishCueSeconds);
            vtt_buf_cur = ngx_snprintf(vtt_buf_cur, vtt_buf_size - (vtt_buf_cur - vtt_buf), "storyboard.jpg#xywh=%d,%d,%d,%d\n", (cur_tile_col-1)*ctx->width, (cur_tile_row-1)*ctx->height, ctx->width, ctx->height);

            cur_tile_col++;
            if (cur_tile_col > ctx->tile_cols) {
                cur_tile_col = 1;
                cur_tile_row++;
            }

            second += ctx->tile_sample_interval;
            need_flush = 1;
            continue;
        }

        need_flush = 0;
        break;
    }

    if (need_flush) {
        if (filter_frame(buffersrc_ctx, buffersink_ctx, NULL, pFrame, log) < 0) {
            goto exit;
        }

        rc = NGX_OK;
    }


    if (rc == NGX_OK) {
        if (cf->tile_vtt) {
            *out_buffer = (caddr_t)vtt_buf;
            *out_len = vtt_buf_cur - vtt_buf;
            rc = NGX_OK;
        } else {
            // Convert the image from its native format to JPEG
            uncompressed_size = pFrame->width * pFrame->height * 3;
            if (ngx_http_video_thumbextractor_jpeg_compress(cf, pFrame->data[0], pFrame->linesize[0], pFrame->width, pFrame->height, out_buffer, out_len, uncompressed_size, temp_pool) == 0) {
                rc = NGX_OK;
            }
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
    if (pCodecCtx != NULL) {
        avcodec_close(pCodecCtx);
        avcodec_free_context(&pCodecCtx);
    }

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
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 10, 100)
    av_register_all();
#endif
#if LIBAVFILTER_VERSION_INT < AV_VERSION_INT(7, 14, 100)
    avfilter_register_all();
#endif
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
    *(dest->size) -= dest->pub.free_in_buffer;
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


float display_aspect_ratio(AVCodecContext *pCodecCtx)
{
    double aspect_ratio = av_q2d(pCodecCtx->sample_aspect_ratio);
    return ((float) pCodecCtx->width / pCodecCtx->height) * (aspect_ratio ? aspect_ratio : 1);
}


int display_width(AVCodecContext *pCodecCtx)
{
    return pCodecCtx->height * display_aspect_ratio(pCodecCtx);
}


int setup_parameters(ngx_http_video_thumbextractor_loc_conf_t *cf, ngx_http_video_thumbextractor_thumb_ctx_t *ctx, AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx)
{
    int64_t remainingTime = ((pFormatCtx->duration / AV_TIME_BASE) - ctx->second);

    if ((ctx->tile_rows != NGX_CONF_UNSET) && (ctx->tile_cols != NGX_CONF_UNSET)) {
        if (cf->tile_sample_interval == NULL) {
            ctx->tile_sample_interval = (pFormatCtx->duration > 0) ? (remainingTime / (ctx->tile_rows * ctx->tile_cols)) + 1 : 5;
        }
    } else if (ctx->tile_rows != NGX_CONF_UNSET) {
        ctx->tile_cols = (pFormatCtx->duration > 0) ? (remainingTime / (ctx->tile_rows * ctx->tile_sample_interval)) + 1 : 1;
        if (ctx->tile_max_cols != NGX_CONF_UNSET) {
            ctx->tile_cols = ngx_min(ctx->tile_cols, ctx->tile_max_cols);
        }
    } else if (ctx->tile_cols != NGX_CONF_UNSET) {
        ctx->tile_rows = (pFormatCtx->duration > 0) ? (remainingTime / (ctx->tile_cols * ctx->tile_sample_interval)) + 1 : 1;
        if (ctx->tile_max_rows != NGX_CONF_UNSET) {
            ctx->tile_rows = ngx_min(ctx->tile_rows, ctx->tile_max_rows);
        }
    } else {
        ctx->tile_rows = 1;
        ctx->tile_cols = 1;
    }

    return NGX_OK;
}


int setup_filters(ngx_http_video_thumbextractor_thumb_ctx_t *ctx, AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx, int videoStream, AVFilterGraph **fg, AVFilterContext **buffersrc_ctx, AVFilterContext **buffersink_ctx, ngx_log_t *log)
{
    AVFilterGraph   *filter_graph = NULL;

    AVFilterContext *transpose_ctx = NULL;
    AVFilterContext *transpose_cw_ctx = NULL;
    AVFilterContext *scale_ctx = NULL;
    AVFilterContext *crop_ctx = NULL;
    AVFilterContext *tile_ctx = NULL;
    AVFilterContext *format_ctx = NULL;

    int              rc = 0;
    char             args[512];

    unsigned int     needs_crop = 0;
    int              crop_x = 0, crop_y = 0;
    float            new_aspect_ratio = 0.0, scale_sws = 0.0, scale_w = 0.0, scale_h = 0.0;
    int              scale_width = 0, scale_height = 0;

    unsigned int     rotate90 = 0, rotate180 = 0, rotate270 = 0;
    int              rotate_value = 0;

    AVDictionaryEntry *rotate = av_dict_get(pFormatCtx->streams[videoStream]->metadata, "rotate", NULL, 0);
    if (rotate) {
        rotate_value = ngx_atoi((u_char *) rotate->value, ngx_strlen(rotate->value));
    } else {
        int32_t *displaymatrix = (int32_t *) av_stream_get_side_data(pFormatCtx->streams[videoStream], AV_PKT_DATA_DISPLAYMATRIX, NULL);
        if (displaymatrix) {
            rotate_value = -1 * av_display_rotation_get(displaymatrix);
        }
    }

    if (rotate_value) {
        rotate90 = rotate_value == 90;
        rotate180 = rotate_value == 180 || rotate_value == -180;
        rotate270 = rotate_value == 270 || rotate_value == -90;
    }

    float aspect_ratio = display_aspect_ratio(pCodecCtx);
    int width = display_width(pCodecCtx);
    int height = pCodecCtx->height;

    if (rotate90 || rotate270) {
        height = width;
        width = pCodecCtx->height;
        aspect_ratio = 1.0 / aspect_ratio;
    }

    if (ctx->height == 0) {
        // keep original format
        ctx->width = width;
        ctx->height = height;
    } else if (ctx->width == 0) {
        // calculate width related with original aspect
        ctx->width = ctx->height * aspect_ratio;
    }

    if ((ctx->width < 16) || (ctx->height < 16)) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Very small size requested, %d x %d", ctx->width, ctx->height);
        return NGX_ERROR;
    }

    new_aspect_ratio = (float) ctx->width / ctx->height;

    scale_width = ctx->width;
    scale_height = ctx->height;

    if (aspect_ratio != new_aspect_ratio) {
        scale_w = (float) ctx->width / width;
        scale_h = (float) ctx->height / height;
        scale_sws = (scale_w > scale_h) ? scale_w : scale_h;

        scale_width = width * scale_sws + 0.5;
        scale_height = height * scale_sws + 0.5;

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

    if ((rotate90 || rotate180 || rotate270) && (avfilter_graph_create_filter(&transpose_ctx, avfilter_get_by_name("transpose"), NULL, rotate270 ? "2" : "1", NULL, filter_graph) < 0)) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: error initializing transpose filter");
        return NGX_ERROR;
    }

    if (rotate180 && (avfilter_graph_create_filter(&transpose_cw_ctx, avfilter_get_by_name("transpose"), NULL, "1", NULL, filter_graph) < 0)) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: error initializing transpose filter");
        return NGX_ERROR;
    }

    snprintf(args, sizeof(args), "%d:%d:flags=bicubic", scale_width, scale_height);
    if (avfilter_graph_create_filter(&scale_ctx, avfilter_get_by_name("scale"), NULL, args, NULL, filter_graph) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: error initializing scale filter");
        return NGX_ERROR;
    }

    if (needs_crop) {
        crop_x = (float) (scale_width - ctx->width) / 2 + 0.5;
        crop_y = (float) (scale_height - ctx->height) / 2 + 0.5;

        snprintf(args, sizeof(args), "%d:%d:%d:%d", (int) ctx->width, (int) ctx->height, crop_x, crop_y);
        if (avfilter_graph_create_filter(&crop_ctx, avfilter_get_by_name("crop"), NULL, args, NULL, filter_graph) < 0) {
            ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: error initializing crop filter");
            return NGX_ERROR;
        }
    }

    ngx_snprintf((u_char *) args, sizeof(args), "%dx%d:margin=%d:padding=%d:color=%V%Z", ctx->tile_cols, ctx->tile_rows, ctx->tile_margin, ctx->tile_padding, &ctx->tile_color);
    if (avfilter_graph_create_filter(&tile_ctx, avfilter_get_by_name("tile"), NULL, args, NULL, filter_graph) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: error initializing tile filter");
        return NGX_ERROR;
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
    if (rotate_value) {
        rc = avfilter_link(*buffersrc_ctx, 0, transpose_ctx, 0);
        if (rotate180) {
            if (rc >= 0) rc = avfilter_link(transpose_ctx, 0, transpose_cw_ctx, 0);
            if (rc >= 0) rc = avfilter_link(transpose_cw_ctx, 0, scale_ctx, 0);
        } else {
            if (rc >= 0) rc = avfilter_link(transpose_ctx, 0, scale_ctx, 0);
        }
    } else {
        rc = avfilter_link(*buffersrc_ctx, 0, scale_ctx, 0);
    }

    if (needs_crop) {
        if (rc >= 0) rc = avfilter_link(scale_ctx, 0, crop_ctx, 0);
        if (rc >= 0) rc = avfilter_link(crop_ctx, 0, tile_ctx, 0);
    } else {
        if (rc >= 0) rc = avfilter_link(scale_ctx, 0, tile_ctx, 0);
    }

    if (rc >= 0) rc = avfilter_link(tile_ctx, 0, format_ctx, 0);
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


int get_frame(ngx_http_video_thumbextractor_loc_conf_t *cf, AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx, AVFrame *pFrame, int videoStream, int64_t second, ngx_log_t *log)
{
    AVPacket packet;
    int      frameFinished = 0;
    int      rc;
    int      decodeStatus;

    int64_t second_on_stream_time_base = second * pFormatCtx->streams[videoStream]->time_base.den / pFormatCtx->streams[videoStream]->time_base.num;

    if ((pFormatCtx->duration > 0) && ((((float_t) pFormatCtx->duration / AV_TIME_BASE) - second)) < 0.1) {
        return NGX_HTTP_VIDEO_THUMBEXTRACTOR_SECOND_NOT_FOUND;
    }

    if (av_seek_frame(pFormatCtx, videoStream, second_on_stream_time_base, cf->next_time ? 0 : AVSEEK_FLAG_BACKWARD) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Seek to an invalid time");
        return NGX_HTTP_VIDEO_THUMBEXTRACTOR_SECOND_NOT_FOUND;
    }

    rc = NGX_HTTP_VIDEO_THUMBEXTRACTOR_SECOND_NOT_FOUND;
    // Find the nearest frame
    while (!frameFinished && av_read_frame(pFormatCtx, &packet) >= 0) {
        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream) {
            // Decode video frame
            if (avcodec_send_packet(pCodecCtx, &packet) == AVERROR(EAGAIN)) continue;
            if ((decodeStatus = avcodec_receive_frame(pCodecCtx, pFrame)) == AVERROR(EAGAIN)) continue;
            // Did we get a video frame?
            if (decodeStatus == 0) {
                rc = NGX_OK;
                if (!cf->only_keyframe && (pFrame->pts < second_on_stream_time_base)) {
                    frameFinished = 0;
                } else {
                    frameFinished = 1;
                }
            }
        }
        // Free the packet that was allocated by av_read_frame
        av_packet_unref(&packet);
    }
    av_packet_unref(&packet);

    return rc;
}
