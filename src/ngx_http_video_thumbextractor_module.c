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
 * ngx_http_video_thumbextractor_module.c
 *
 * Created:  Nov 22, 2011
 * Author:   Wandenberg Peixoto <wandenberg@gmail.com>
 *
 */
#include <ngx_http_video_thumbextractor_module.h>
#include <ngx_http_video_thumbextractor_module_setup.c>
#include <ngx_http_video_thumbextractor_module_utils.c>

static ngx_int_t
ngx_http_video_thumbextractor_handler(ngx_http_request_t *r)
{
    ngx_http_video_thumbextractor_loc_conf_t    *vtlcf;
    ngx_str_t                                    vv_filename = ngx_null_string, vv_second = ngx_null_string;
    ngx_str_t                                    vv_width = ngx_null_string, vv_height = ngx_null_string;
    ngx_str_t                                   *filename;
    ngx_int_t                                    rc, second = 0, width = 0, height = 0;
    caddr_t                                      out_buffer = 0;
    size_t                                       out_len = 0;
    ngx_buf_t                                   *b;
    ngx_chain_t                                 *out;

    vtlcf = ngx_http_get_module_loc_conf(r, ngx_http_video_thumbextractor_module);

    ngx_http_core_loc_conf_t            *clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    // check if received a filename
    ngx_http_complex_value(r, vtlcf->video_filename, &vv_filename);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_VARIABLE_REQUIRED(vv_filename, r->connection->log, "filename variable is empty");

    ngx_http_complex_value(r, vtlcf->video_second, &vv_second);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_VARIABLE_REQUIRED(vv_second, r->connection->log, "second variable is empty");

    if (vtlcf->image_width != NULL) {
        ngx_http_complex_value(r, vtlcf->image_width, &vv_width);
        width = ngx_atoi(vv_width.data, vv_width.len);
        width = (width != NGX_ERROR) ? width : 0;
    }

    if (vtlcf->image_height != NULL) {
        ngx_http_complex_value(r, vtlcf->image_height, &vv_height);
        height = ngx_atoi(vv_height.data, vv_height.len);
        height = (height != NGX_ERROR) ? height : 0;
    }

    if (((width > 0) && (width < 16)) || ((height > 0) && (height < 16))) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video thumb extractor module: Very small size requested, %d x %d", width, height);
        return NGX_HTTP_BAD_REQUEST;
    }

    if ((filename = ngx_http_video_thumbextractor_create_str(r->pool, clcf->root.len + vv_filename.len)) == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video thumb extractor module: unable to allocate memory to store full filename");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    ngx_memcpy(ngx_copy(filename->data, clcf->root.data, clcf->root.len), vv_filename.data, vv_filename.len);

    second = ngx_atoi(vv_second.data, vv_second.len);

    if ((rc = ngx_http_video_thumbextractor_get_thumb(vtlcf, (char *)filename->data, second, width, height, &out_buffer, &out_len, r->pool, r->connection->log)) != NGX_OK) {
        if (rc == NGX_ERROR) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        return NGX_HTTP_NOT_FOUND;
    }

    /* write response */
    r->headers_out.content_type = NGX_HTTP_VIDEO_THUMBEXTRACTOR_CONTENT_TYPE;
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = out_len;

    ngx_http_send_header(r);

    out = (ngx_chain_t *) ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
    b = ngx_calloc_buf(r->pool);
    if ((out == NULL) || (b == NULL)) {
        return NGX_ERROR;
    }

    b->last_buf = 1;
    b->flush = 1;
    b->memory = 1;
    b->pos = (u_char *) out_buffer;
    b->start = b->pos;
    b->end = b->pos + out_len;
    b->last = b->end;

    out->buf = b;
    out->next = NULL;

    ngx_http_output_filter(r, out);

    return NGX_DONE;
}
