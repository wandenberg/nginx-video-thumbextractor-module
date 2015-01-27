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
 * ngx_http_video_thumbextractor_module_utils.h
 *
 * Created:  Nov 22, 2011
 * Author:   Wandenberg Peixoto <wandenberg@gmail.com>
 *
 */
#ifndef NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_UTILS_H_
#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_UTILS_H_

static ngx_str_t                              *ngx_http_video_thumbextractor_create_str(ngx_pool_t *pool, uint len);

static int                                     ngx_http_video_thumbextractor_get_thumb(ngx_http_video_thumbextractor_loc_conf_t *cf, ngx_http_video_thumbextractor_ctx_t *ctx, ngx_http_video_thumbextractor_file_info_t *info, caddr_t *out_buffer, size_t *out_len, ngx_pool_t *temp_pool, ngx_log_t *log);
static void                                    ngx_http_video_thumbextractor_init_libraries(void);

#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_FILE_NOT_FOUND   1
#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_SECOND_NOT_FOUND 2

#endif /* NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_UTILS_H_ */
