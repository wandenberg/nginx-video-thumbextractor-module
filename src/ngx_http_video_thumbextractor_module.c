#include <ngx_http_video_thumbextractor_module.h>
#include <ngx_http_video_thumbextractor_module_setup.c>
#include <ngx_http_video_thumbextractor_module_utils.c>

static ngx_int_t
ngx_http_video_thumbextractor_handler(ngx_http_request_t *r)
{
    ngx_http_video_thumbextractor_loc_conf_t    *vtlcf;
    ngx_http_variable_value_t                   *vv_filename, *vv_second, *vv_width, *vv_height;
    ngx_str_t                                   *filename;
    ngx_uint_t                                   second = 0, width = 0, height = 0;
    ngx_int_t                                    rc;
    caddr_t                                      out_buffer = 0;
    size_t                                       out_len = 0;
    ngx_buf_t                                   *b;
    ngx_chain_t                                 *out;

    vtlcf = ngx_http_get_module_loc_conf(r, ngx_http_video_thumbextractor_module);

    ngx_http_core_loc_conf_t            *clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    // check if received a filename
    vv_filename = ngx_http_get_indexed_variable(r, vtlcf->index_video_filename);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_VARIABLE_REQUIRED(vv_filename, r->connection->log, "filename variable is empty");

    vv_second = ngx_http_get_indexed_variable(r, vtlcf->index_video_second);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_VARIABLE_REQUIRED(vv_second, r->connection->log, "second variable is empty");

    vv_width = ngx_http_get_indexed_variable(r, vtlcf->index_image_width);
    if ((vv_width != NULL) && !vv_width->not_found && (vv_width->len > 0)) {
        width = ngx_atoi(vv_width->data, vv_width->len);
    }

    vv_height = ngx_http_get_indexed_variable(r, vtlcf->index_image_height);
    if ((vv_height != NULL) && !vv_height->not_found && (vv_height->len > 0)) {
        height = ngx_atoi(vv_height->data, vv_height->len);
    }

    if ((filename = ngx_http_video_thumbextractor_create_str(r->pool, clcf->root.len + vv_filename->len)) == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video thumb extractor module: unable to allocate memory to store full filename");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    ngx_memcpy(ngx_copy(filename->data, clcf->root.data, clcf->root.len), vv_filename->data, vv_filename->len);

    second = ngx_atoi(vv_second->data, vv_second->len);


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
