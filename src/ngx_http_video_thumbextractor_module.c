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

ngx_http_output_header_filter_pt ngx_http_video_thumbextractor_next_header_filter;
ngx_http_output_body_filter_pt ngx_http_video_thumbextractor_next_body_filter;

ngx_int_t
ngx_http_video_thumbextractor_access_handler(ngx_http_request_t *r)
{
    ngx_http_video_thumbextractor_loc_conf_t    *vtlcf;
    ngx_http_video_thumbextractor_ctx_t         *ctx;
    ngx_str_t                                    vv_filename = ngx_null_string, vv_second = ngx_null_string;
    ngx_str_t                                    vv_width = ngx_null_string, vv_height = ngx_null_string;

    vtlcf = ngx_http_get_module_loc_conf(r, ngx_http_video_thumbextractor_module);

    ngx_http_core_loc_conf_t            *clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (!vtlcf->enabled) {
        return NGX_OK;
    }

    if ((ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_video_thumbextractor_ctx_t))) == NULL) {
        return NGX_ERROR;
    }

    // check if received a filename
    ngx_http_complex_value(r, vtlcf->video_filename, &vv_filename);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_VARIABLE_REQUIRED(vv_filename, r->connection->log, "filename variable is empty");

    ngx_http_complex_value(r, vtlcf->video_second, &vv_second);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_VARIABLE_REQUIRED(vv_second, r->connection->log, "second variable is empty");

    ctx->second = ngx_atoi(vv_second.data, vv_second.len);
    if (ctx->second == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video thumb extractor module: Invalid second %V", &vv_second);
        return NGX_HTTP_BAD_REQUEST;
    }

    if (vtlcf->image_width != NULL) {
        ngx_http_complex_value(r, vtlcf->image_width, &vv_width);
        ctx->width = ngx_atoi(vv_width.data, vv_width.len);
        ctx->width = (ctx->width != NGX_ERROR) ? ctx->width : 0;
    }

    if (vtlcf->image_height != NULL) {
        ngx_http_complex_value(r, vtlcf->image_height, &vv_height);
        ctx->height = ngx_atoi(vv_height.data, vv_height.len);
        ctx->height = (ctx->height != NGX_ERROR) ? ctx->height : 0;
    }

    if (((ctx->width > 0) && (ctx->width < 16)) || ((ctx->height > 0) && (ctx->height < 16))) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video thumb extractor module: Very small size requested, %d x %d", ctx->width, ctx->height);
        return NGX_HTTP_BAD_REQUEST;
    }

    if ((ctx->filename = ngx_http_video_thumbextractor_create_str(r->pool, clcf->root.len + vv_filename.len)) == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video thumb extractor module: unable to allocate memory to store full filename");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    ngx_memcpy(ngx_copy(ctx->filename->data, clcf->root.data, clcf->root.len), vv_filename.data, vv_filename.len);

    ngx_http_set_ctx(r, ctx, ngx_http_video_thumbextractor_module);

    return NGX_OK;
}


static ngx_int_t ngx_http_video_thumbextractor_header_filter(ngx_http_request_t *r)
{
    ngx_http_video_thumbextractor_loc_conf_t *vtlcf;
    ngx_http_video_thumbextractor_ctx_t      *ctx;

    vtlcf = ngx_http_get_module_loc_conf(r, ngx_http_video_thumbextractor_module);

    if (!vtlcf->enabled) {
        return ngx_http_video_thumbextractor_next_header_filter(r);
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_video_thumbextractor_module);

    if (ctx == NULL) {
        return ngx_http_video_thumbextractor_next_header_filter(r);
    }

    return NGX_OK;
}


static ngx_int_t ngx_http_video_thumbextractor_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_http_video_thumbextractor_loc_conf_t *vtlcf;
    ngx_http_video_thumbextractor_ctx_t      *ctx;
    ngx_chain_t                              *cl;
    ngx_int_t                                 rc;
    caddr_t                                   out_buffer = 0;
    size_t                                    out_len = 0;
    ngx_buf_t                                *b;
    ngx_chain_t                              *out;
    ngx_flag_t                                last_buf = 0;

    ngx_http_video_thumbextractor_file_info_t info;

    vtlcf = ngx_http_get_module_loc_conf(r, ngx_http_video_thumbextractor_module);

    if (!vtlcf->enabled) {
        return ngx_http_video_thumbextractor_next_body_filter(r, in);
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_video_thumbextractor_module);

    if (ctx == NULL) {
        return ngx_http_video_thumbextractor_next_body_filter(r, in);
    }

    if (in == NULL) {
        return ngx_http_video_thumbextractor_next_body_filter(r, in);
    }

    // discard chains from original content
    for (cl = in; cl; cl = cl->next) {
        cl->buf->pos = cl->buf->last;
        last_buf = (cl->buf->last_buf) ? 1 : last_buf;
    }

    if (!last_buf) {
        return NGX_OK;
    }

    // clear values from original content
    ngx_http_clear_content_length(r);
    ngx_http_clear_accept_ranges(r);
    ngx_http_clear_last_modified(r);

    ngx_http_set_ctx(r, NULL, ngx_http_video_thumbextractor_module);

    info.filename = ctx->filename;
    info.offset = 0;

#if (NGX_HTTP_CACHE)
    if (r->cache && (r->headers_out.status == NGX_HTTP_OK)) {
        if ((info.filename = ngx_http_video_thumbextractor_create_str(r->pool, r->cache->file.name.len)) == NULL) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video thumb extractor module: unable to allocate memory to copy proxy cache full filename");
            return ngx_http_filter_finalize_request(r, &ngx_http_video_thumbextractor_module, NGX_HTTP_INTERNAL_SERVER_ERROR);
        }

        ngx_memcpy(info.filename->data, r->cache->file.name.data, r->cache->file.name.len);
        info.offset = r->cache->body_start;
    }
#endif

    if ((rc = ngx_http_video_thumbextractor_get_thumb(vtlcf, ctx, &info, &out_buffer, &out_len, r->pool, r->connection->log)) != NGX_OK) {
        if (rc == NGX_ERROR) {
            return ngx_http_filter_finalize_request(r, &ngx_http_video_thumbextractor_module, NGX_HTTP_INTERNAL_SERVER_ERROR);
        }
        return ngx_http_filter_finalize_request(r, &ngx_http_video_thumbextractor_module, NGX_HTTP_NOT_FOUND);
    }

    /* write response */
    r->headers_out.content_type = NGX_HTTP_VIDEO_THUMBEXTRACTOR_CONTENT_TYPE;
    r->headers_out.content_type_len = NGX_HTTP_VIDEO_THUMBEXTRACTOR_CONTENT_TYPE.len;
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = out_len;

    out = (ngx_chain_t *) ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
    b = ngx_calloc_buf(r->pool);
    if ((out == NULL) || (b == NULL)) {
        return NGX_ERROR;
    }

    b->last_buf = 1;
    b->last_in_chain = 1;
    b->flush = 1;
    b->memory = 1;
    b->pos = (u_char *) out_buffer;
    b->start = b->pos;
    b->end = b->pos + out_len;
    b->last = b->end;

    out->buf = b;
    out->next = NULL;

    rc = ngx_http_video_thumbextractor_next_header_filter(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_video_thumbextractor_next_body_filter(r, out);
}


ngx_int_t
ngx_http_video_thumbextractor_filter_init(ngx_conf_t *cf)
{
    ngx_http_video_thumbextractor_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_video_thumbextractor_header_filter;

    ngx_http_video_thumbextractor_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_video_thumbextractor_body_filter;

    return NGX_OK;
}
