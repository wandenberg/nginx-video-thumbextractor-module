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
#include <jpeglib.h>
#include <wand/magick_wand.h>

#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_BUFFER_SIZE 1024 * 8
#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_MEMORY_STEP 1024
#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_RGB         "RGB"

static uint32_t     ngx_http_video_thumbextractor_jpeg_compress(ngx_http_video_thumbextractor_loc_conf_t *cf, uint8_t * buffer, int in_width, int in_height, int out_width, int out_height, caddr_t *out_buffer, size_t *out_len, size_t uncompressed_size, ngx_pool_t *temp_pool);
static void         ngx_http_video_thumbextractor_jpeg_memory_dest (j_compress_ptr cinfo, caddr_t *out_buf, size_t *out_size, size_t uncompressed_size, ngx_pool_t *temp_pool);

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
        return fseek(info->fd, info->offset + offset, whence);
    }
    return -1;
}


int ngx_http_video_thumbextractor_read_data_from_file(void *opaque, uint8_t *buf, int buf_len)
{
    ngx_http_video_thumbextractor_file_info_t *info = (ngx_http_video_thumbextractor_file_info_t *) opaque;

    if ((info->offset > 0) && (ftell(info->fd) <= 0)) {
        fseek(info->fd, info->offset, SEEK_SET);
    }

    int r = fread(buf, sizeof(uint8_t), buf_len, info->fd);
    return (r == -1) ? AVERROR(errno) : r;
}


static int
ngx_http_video_thumbextractor_get_thumb(ngx_http_video_thumbextractor_loc_conf_t *cf, ngx_http_video_thumbextractor_file_info_t *info, int64_t second, ngx_uint_t width, ngx_uint_t height, caddr_t *out_buffer, size_t *out_len, ngx_pool_t *temp_pool, ngx_log_t *log)
{
    int              rc, videoStream, frameFinished = 0;
    unsigned int     i;
    AVFormatContext *pFormatCtx = NULL;
    AVCodecContext  *pCodecCtx = NULL;
    AVCodec         *pCodec = NULL;
    AVFrame         *pFrame = NULL, *pFrameRGB = NULL;
    uint8_t         *buffer = NULL;
    AVPacket         packet;
    size_t           uncompressed_size;
    float            scale = 0.0, new_scale = 0.0, scale_sws = 0.0, scale_w = 0.0, scale_h = 0.0;
    int              sws_width = 0, sws_height = 0;
    ngx_flag_t       needs_crop = 0;
    MagickWand      *m_wand = NULL;
    MagickBooleanType mrc;
    unsigned char   *bufferAVIO = NULL;
    AVIOContext     *pAVIOCtx = NULL;
    char            *filename = (char *) info->filename->data;

    // Open video file
    if ((info->fd = fopen(filename, "rb")) == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Couldn't open file %s", filename);
        rc = EXIT_FAILURE;
        goto exit;
    }

    // Get file size
    fseek(info->fd, 0, SEEK_END);
    info->size = ftell(info->fd) - info->offset;
    fseek(info->fd, 0, SEEK_SET);

    pFormatCtx = avformat_alloc_context();
    bufferAVIO = (unsigned char *) av_malloc(NGX_HTTP_VIDEO_THUMBEXTRACTOR_BUFFER_SIZE * sizeof(unsigned char));
    if ((pFormatCtx == NULL) || (bufferAVIO == NULL)) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Couldn't alloc AVIO buffer");
        rc = NGX_ERROR;
        goto exit;
    }

    pAVIOCtx = avio_alloc_context(bufferAVIO, NGX_HTTP_VIDEO_THUMBEXTRACTOR_BUFFER_SIZE, 0, info, ngx_http_video_thumbextractor_read_data_from_file, NULL, ngx_http_video_thumbextractor_seek_data_from_file);
    if (pAVIOCtx == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Couldn't alloc AVIO context");
        rc = NGX_ERROR;
        goto exit;
    }

    pFormatCtx->pb = pAVIOCtx;

    // Open video file
    if ((rc = avformat_open_input(&pFormatCtx, filename, NULL, NULL)) != 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Couldn't open file %s, error: %d", filename, rc);
        rc = (rc == AVERROR(NGX_ENOENT)) ? NGX_HTTP_VIDEO_THUMBEXTRACTOR_FILE_NOT_FOUND : NGX_ERROR;
        goto exit;
    }

    // Retrieve stream information
#if LIBAVFORMAT_VERSION_INT <= AV_VERSION_INT(53, 5, 0)
    if (av_find_stream_info(pFormatCtx) < 0) {
#else
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
#endif
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Couldn't find stream information");
        rc = NGX_ERROR;
        goto exit;
    }

    if ((pFormatCtx->duration > 0) && (second > (pFormatCtx->duration / AV_TIME_BASE))) {
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
        rc = NGX_ERROR;
        goto exit;
    }

    // Get a pointer to the codec context for the video stream
    pCodecCtx = pFormatCtx->streams[videoStream]->codec;

    // Find the decoder for the video stream
    if ((pCodec = avcodec_find_decoder(pCodecCtx->codec_id)) == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Codec %d not found", pCodecCtx->codec_id);
        rc = NGX_ERROR;
        goto exit;
    }

    // Open codec
#if LIBAVCODEC_VERSION_INT <= AV_VERSION_INT(53, 8, 0)
    if ((rc = avcodec_open(pCodecCtx, pCodec)) < 0) {
#else
    if ((rc = avcodec_open2(pCodecCtx, pCodec, NULL)) < 0) {
#endif
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Could not open codec, error %d", rc);
        rc = NGX_ERROR;
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
        rc = NGX_ERROR;
        goto exit;
    }

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


    // Allocate video frame
    pFrame = avcodec_alloc_frame();

    // Allocate an AVFrame structure
    pFrameRGB = avcodec_alloc_frame();
    if ((pFrame == NULL) || (pFrameRGB == NULL)) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Could not alloc frame memory");
        rc = NGX_ERROR;
        goto exit;
    }

    // Determine required buffer size and allocate buffer
    uncompressed_size = avpicture_get_size(PIX_FMT_RGB24, sws_width, sws_height) * sizeof(uint8_t);
    buffer = (uint8_t *) av_malloc(uncompressed_size);

    // Assign appropriate parts of buffer to image planes in pFrameRGB
    // Note that pFrameRGB is an AVFrame, but AVFrame is a superset of AVPicture
    avpicture_fill((AVPicture *) pFrameRGB, buffer, PIX_FMT_RGB24, sws_width, sws_height);

    if ((rc = av_seek_frame(pFormatCtx, -1, second * AV_TIME_BASE, cf->next_time ? 0 : AVSEEK_FLAG_BACKWARD)) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Seek to an invalid time, error: %d", rc);
        rc = NGX_HTTP_VIDEO_THUMBEXTRACTOR_SECOND_NOT_FOUND;
        goto exit;
    }

    int64_t second_on_stream_time_base = second * pFormatCtx->streams[videoStream]->time_base.den / pFormatCtx->streams[videoStream]->time_base.num;

    rc = NGX_HTTP_VIDEO_THUMBEXTRACTOR_SECOND_NOT_FOUND;
    while (!frameFinished && av_read_frame(pFormatCtx, &packet) >= 0) {
        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream) {
            // Decode video frame
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
            // Did we get a video frame?
            if (frameFinished) {
                if (!cf->only_keyframe && (pFrame->pkt_pts < second_on_stream_time_base)) {
                    frameFinished = 0;
                    av_free_packet(&packet);
                    continue;
                }

                // Convert the image from its native format to RGB
                struct SwsContext *img_resample_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                        sws_width, sws_height, PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);

                sws_scale(img_resample_ctx, (const uint8_t * const *) pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);
                sws_freeContext(img_resample_ctx);


                if (needs_crop) {
                    MagickWandGenesis();
                    mrc = MagickTrue;

                    if ((m_wand = NewMagickWand()) == NULL){
                        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Could not allocate MagickWand memory");
                        mrc = MagickFalse;
                    }

                    if (mrc == MagickTrue) {
                        mrc = MagickConstituteImage(m_wand, sws_width, sws_height, NGX_HTTP_VIDEO_THUMBEXTRACTOR_RGB, CharPixel, pFrameRGB->data[0]);
                    }

                    if (mrc == MagickTrue) {
                        mrc = MagickSetImageGravity(m_wand, CenterGravity);
                    }

                    if (mrc == MagickTrue) {
                        mrc = MagickCropImage(m_wand, width, height, (sws_width-width)/2, (sws_height-height)/2);
                    }

                    if (mrc == MagickTrue) {
                        mrc = MagickExportImagePixels(m_wand, 0, 0, width, height, NGX_HTTP_VIDEO_THUMBEXTRACTOR_RGB, CharPixel, pFrameRGB->data[0]);
                    }

                    /* Clean up */
                    if (m_wand) {
                        m_wand = DestroyMagickWand(m_wand);
                    }

                    MagickWandTerminus();

                    if (mrc != MagickTrue) {
                        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Error cropping image");
                        /* stop the while before try to jpeg compress the image */
                        break;
                    }
                }

                // Compress to jpeg
                if (ngx_http_video_thumbextractor_jpeg_compress(cf, pFrameRGB->data[0], pCodecCtx->width, pCodecCtx->height, width, height, out_buffer, out_len, uncompressed_size, temp_pool) == 0) {
                    rc = NGX_OK;
                }
            }
        }

        // Free the packet that was allocated by av_read_frame
        av_free_packet(&packet);
    }
    av_free_packet(&packet);

exit:

    if ((info->fd != NULL) && (fclose(info->fd) != 0)) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Couldn't close file %s", filename);
        rc = EXIT_FAILURE;
    }

    /* destroy unneeded objects */

    // Free the RGB image
    if (buffer != NULL) av_free(buffer);
    if (pFrameRGB != NULL) av_free(pFrameRGB);

    // Free the YUV frame
    if (pFrame != NULL) av_free(pFrame);

    // Close the codec
    if (pCodecCtx != NULL) avcodec_close(pCodecCtx);

    // Close the video file
    if (pFormatCtx != NULL) {
#if LIBAVFORMAT_VERSION_INT <= AV_VERSION_INT(53, 5, 0)
        av_close_input_file(pFormatCtx);
#else
        avformat_close_input(&pFormatCtx);
#endif
    }

    // Free AVIO context
    if (pAVIOCtx != NULL) av_free(pAVIOCtx);

    return rc;
}


static void
ngx_http_video_thumbextractor_init_libraries(void)
{
    // Register all formats and codecs
    av_register_all();
    av_log_set_level(AV_LOG_ERROR);
}


static uint32_t
ngx_http_video_thumbextractor_jpeg_compress(ngx_http_video_thumbextractor_loc_conf_t *cf, uint8_t * buffer, int in_width, int in_height, int out_width, int out_height, caddr_t *out_buffer, size_t *out_len, size_t uncompressed_size, ngx_pool_t *temp_pool)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];
    int row_stride;
    int image_d_width = in_width;
    int image_d_height = in_height;

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
    /* Image DPI is determined by Y_density, so we leave that at
       jpeg_dpi if possible and crunch X_density instead (PAR > 1) */

    if (out_height * image_d_width > out_width * image_d_height) {
        image_d_width = out_height * image_d_width / image_d_height;
        image_d_height = out_height;
    } else {
        image_d_height = out_width * image_d_height / image_d_width;
        image_d_width = out_width;
    }

    cinfo.X_density = cf->jpeg_dpi * out_width / image_d_width;
    cinfo.Y_density = cf->jpeg_dpi * out_height / image_d_height;
    cinfo.write_Adobe_marker = TRUE;

    jpeg_set_quality(&cinfo, cf->jpeg_quality, cf->jpeg_baseline);
    cinfo.optimize_coding = cf->jpeg_optimize;
    cinfo.smoothing_factor = cf->jpeg_smooth;

    if ( cf->jpeg_progressive_mode ) {
        jpeg_simple_progression(&cinfo);
    }

    jpeg_start_compress(&cinfo, TRUE);

    row_stride = out_width * 3;
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &buffer[cinfo.next_scanline * row_stride];
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
