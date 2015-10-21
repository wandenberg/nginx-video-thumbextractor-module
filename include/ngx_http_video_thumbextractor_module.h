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
 * ngx_http_video_thumbextractor_module.h
 *
 * Created:  Nov 22, 2011
 * Author:   Wandenberg Peixoto <wandenberg@gmail.com>
 *
 */
#ifndef NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_H_
#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_H_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
    ngx_uint_t                              processes_per_worker;
} ngx_http_video_thumbextractor_main_conf_t;

typedef struct {
    ngx_http_complex_value_t               *video_filename;
    ngx_http_complex_value_t               *video_second;
    ngx_http_complex_value_t               *image_width;
    ngx_http_complex_value_t               *image_height;

    ngx_uint_t                              tile_sample_interval;
    ngx_uint_t                              tile_cols;
    ngx_uint_t                              tile_max_cols;
    ngx_uint_t                              tile_rows;
    ngx_uint_t                              tile_max_rows;
    ngx_uint_t                              tile_margin;
    ngx_uint_t                              tile_padding;
    ngx_str_t                               tile_color;

    ngx_uint_t                              jpeg_baseline;
    ngx_uint_t                              jpeg_progressive_mode;
    ngx_uint_t                              jpeg_optimize;
    ngx_uint_t                              jpeg_smooth;
    ngx_uint_t                              jpeg_quality;
    ngx_uint_t                              jpeg_dpi;

    ngx_flag_t                              only_keyframe;
    ngx_flag_t                              next_time;

    ngx_str_t                               threads;

    ngx_flag_t                              enabled;
} ngx_http_video_thumbextractor_loc_conf_t;

typedef struct {
    int64_t                          size;
    int64_t                          offset;
    ngx_file_t                       file;
} ngx_http_video_thumbextractor_file_info_t;

typedef struct {
    ngx_http_video_thumbextractor_file_info_t   file_info;
    ngx_int_t                                   second;
    ngx_int_t                                   width;
    ngx_int_t                                   height;
    ngx_uint_t                                  tile_sample_interval;
    ngx_uint_t                                  tile_cols;
    ngx_uint_t                                  tile_rows;
    ngx_str_t                                   filename;
} ngx_http_video_thumbextractor_thumb_ctx_t;

typedef enum {
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_RC = 1,
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_IMAGE_LEN,
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_IMAGE_DATA,
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_FINISHED
} ngx_http_video_thumbextractor_transfer_step;

typedef struct {
    ngx_http_video_thumbextractor_transfer_step     step;
    ngx_buf_t                                       buffer;
    ngx_http_video_thumbextractor_thumb_ctx_t       thumb_ctx;
    caddr_t                                         data;
    size_t                                          size;
    ngx_int_t                                       rc;
    ngx_pool_t                                     *pool;
    ngx_connection_t                               *conn;
} ngx_http_video_thumbextractor_transfer_t;

typedef struct {
    ngx_queue_t                                 queue;
    ngx_int_t                                   slot;
    ngx_http_request_t                         *request;
    ngx_http_video_thumbextractor_thumb_ctx_t   thumb_ctx;
    ngx_http_video_thumbextractor_transfer_t    transfer;
} ngx_http_video_thumbextractor_ctx_t;

ngx_int_t ngx_http_video_thumbextractor_access_handler(ngx_http_request_t *r);
ngx_int_t ngx_http_video_thumbextractor_filter_init(ngx_conf_t *cf);


static ngx_str_t NGX_HTTP_VIDEO_THUMBEXTRACTOR_CONTENT_TYPE = ngx_string("image/jpeg");

#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_VARIABLE_REQUIRED(variable, log, msg)          \
    if (variable.len == 0) {                                                         \
        ngx_log_error(NGX_LOG_ERR, log, 0, "video thumb extractor module: %s", msg); \
        return NGX_HTTP_BAD_REQUEST;                                                 \
    }

#endif /* NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_H_ */
