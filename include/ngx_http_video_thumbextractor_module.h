#ifndef NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_H_
#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_H_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <video_hash.h>

typedef struct {
    ngx_int_t                               index_video_filename;
    ngx_int_t                               index_video_second;
    ngx_int_t                               index_image_width;
    ngx_int_t                               index_image_height;

    ngx_uint_t                              jpeg_baseline;
    ngx_uint_t                              jpeg_progressive_mode;
    ngx_uint_t                              jpeg_optimize;
    ngx_uint_t                              jpeg_smooth;
    ngx_uint_t                              jpeg_quality;
    ngx_uint_t                              jpeg_dpi;

    ngx_flag_t                              enabled;
} ngx_http_video_thumbextractor_loc_conf_t;

static ngx_int_t ngx_http_video_thumbextractor_handler(ngx_http_request_t *r);

static ngx_str_t NGX_HTTP_VIDEO_THUMBEXTRACTOR_CONTENT_TYPE = ngx_string("image/jpeg");

#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_VARIABLE_REQUIRED(variable, log, msg)          \
	if ((variable == NULL) || variable->not_found || (variable->len == 0)) {         \
		ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: %s", msg); \
		return NGX_HTTP_BAD_REQUEST;                                                 \
	}

#endif /* NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_H_ */
