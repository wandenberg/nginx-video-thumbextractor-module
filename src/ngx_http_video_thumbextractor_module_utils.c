#include <ngx_http_video_thumbextractor_module_utils.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <jpeglib.h>

#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_MEMORY_STEP 1024

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


static int
ngx_http_video_thumbextractor_get_thumb(ngx_http_video_thumbextractor_loc_conf_t *cf, const char *filename, int64_t second, ngx_uint_t width, ngx_uint_t height, caddr_t *out_buffer, size_t *out_len, ngx_pool_t *temp_pool, ngx_log_t *log)
{
    int              rc, videoStream, frameFinished;
    unsigned int     i;
    AVFormatContext *pFormatCtx = NULL;
    AVCodecContext  *pCodecCtx = NULL;
    AVCodec         *pCodec = NULL;
    AVFrame         *pFrame = NULL, *pFrameRGB = NULL;
    uint8_t         *buffer = NULL;
    AVPacket         packet;
    size_t           uncompressed_size;

    // Open video file
    if ((rc = av_open_input_file(&pFormatCtx, filename, NULL, 0, NULL)) != 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Couldn't open file %s, error: %d", filename, rc);
        rc = (rc == AVERROR_NOENT) ? NGX_HTTP_VIDEO_THUMBEXTRACTOR_FILE_NOT_FOUND : NGX_ERROR;
        goto exit;
    }

    // Retrieve stream information
    if (av_find_stream_info(pFormatCtx) < 0) {
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
    if ((rc = avcodec_open(pCodecCtx, pCodec)) < 0) {
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
        width = ((width + 8) / 16) * 16;
    }

    // Allocate video frame
    pFrame = avcodec_alloc_frame();

    // Allocate an AVFrame structure
    pFrameRGB = avcodec_alloc_frame();
    if ((pFrameRGB == NULL) || (pFrameRGB == NULL)) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Could not alloc frame memory");
        rc = NGX_ERROR;
        goto exit;
    }

    // Determine required buffer size and allocate buffer
    uncompressed_size = avpicture_get_size(PIX_FMT_RGB24, width, height) * sizeof(uint8_t);
    buffer = (uint8_t *) av_malloc(uncompressed_size);

    // Assign appropriate parts of buffer to image planes in pFrameRGB
    // Note that pFrameRGB is an AVFrame, but AVFrame is a superset of AVPicture
    avpicture_fill((AVPicture *) pFrameRGB, buffer, PIX_FMT_RGB24, width, height);

    if ((rc = av_seek_frame(pFormatCtx, -1, second * AV_TIME_BASE, 0)) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: Seek to an invalid time, error: %d", rc);
        rc = NGX_HTTP_VIDEO_THUMBEXTRACTOR_SECOND_NOT_FOUND;
        goto exit;
    }

    rc = NGX_ERROR;
    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream) {
            // Decode video frame
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
            // Did we get a video frame?
            if (frameFinished) {
                // Convert the image from its native format to RGB
                struct SwsContext *img_resample_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                        width, height, PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);

                sws_scale(img_resample_ctx, (const uint8_t * const *)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);
                sws_freeContext(img_resample_ctx);

                // Compress to jpeg
                if (ngx_http_video_thumbextractor_jpeg_compress(cf, pFrameRGB->data[0], pCodecCtx->width, pCodecCtx->height, width, height, out_buffer, out_len, uncompressed_size, temp_pool) == 0) {
                    rc = NGX_OK;
                }
                break;
            }
        }

        // Free the packet that was allocated by av_read_frame
        av_free_packet(&packet);
    }
    av_free_packet(&packet);

exit:
    /* destroy unneeded objects */

    // Free the RGB image
    if (buffer != NULL) av_free(buffer);
    if (pFrameRGB != NULL) av_free(pFrameRGB);

    // Free the YUV frame
    if (pFrame != NULL) av_free(pFrame);

    // Close the codec
    if (pCodecCtx != NULL) avcodec_close(pCodecCtx);

    // Close the video file
    if (pFormatCtx != NULL) av_close_input_file(pFormatCtx);

    return rc;
}


static void
ngx_http_video_thumbextractor_init_libraries(void)
{
    // Register all formats and codecs
    av_register_all();
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
