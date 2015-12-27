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

#include <ngx_channel.h>
ngx_http_output_header_filter_pt ngx_http_video_thumbextractor_next_header_filter;
ngx_http_output_body_filter_pt ngx_http_video_thumbextractor_next_body_filter;

void        ngx_http_video_thumbextractor_start_extract(ngx_uint_t slot);
void        ngx_http_video_thumbextractor_send_values_to_process(ngx_event_t *ev);
void        ngx_http_video_thumbextractor_run_extract(ngx_int_t fd);
void        ngx_http_video_thumbextractor_extract_process_parent_read_handler(ngx_event_t *ev);
void        ngx_http_video_thumbextractor_extract_process_child_read_handler(ngx_event_t *ev);
void        ngx_http_video_thumbextractor_extract_process_write_handler(ngx_event_t *ev);
void        ngx_http_video_thumbextractor_cleanup_parent_process(ngx_http_video_thumbextractor_ipc_t *ipc_ctx);
void        ngx_http_video_thumbextractor_cleanup_extract_process(ngx_http_video_thumbextractor_transfer_t *transfer);
ngx_int_t   ngx_http_video_thumbextractor_recv(ngx_connection_t *c, ngx_event_t *rev, ngx_buf_t *buf, ssize_t len);
ngx_int_t   ngx_http_video_thumbextractor_write(ngx_connection_t *c, ngx_event_t *wev, ngx_buf_t *buf, ssize_t len);
void        ngx_http_video_thumbextractor_set_buffer(ngx_buf_t *buf, u_char *start, u_char *last, ssize_t len);
void        ngx_http_video_thumbextractor_sig_handler(int signo);

static ngx_http_video_thumbextractor_transfer_t ngx_http_video_thumbextractor_transfer;
static ngx_socket_t                             ngx_http_video_thumbextractor_transfer_sockets[2];

void
ngx_http_video_thumbextractor_module_ensure_extractor_process(void)
{
    ngx_http_video_thumbextractor_main_conf_t   *vtmcf = ngx_http_cycle_get_module_main_conf(ngx_cycle, ngx_http_video_thumbextractor_module);
    ngx_int_t                                    slot = -1;
    ngx_uint_t                                   i;

    if (ngx_queue_empty(ngx_http_video_thumbextractor_module_extract_queue) || ngx_exiting) {
        return;
    }

    for (i = 0; i < vtmcf->processes_per_worker; ++i) {
        if ((ngx_http_video_thumbextractor_module_ipc_ctxs[i].pid != -1) && (!ngx_http_video_thumbextractor_module_ipc_ctxs[i].processing)) {
            ngx_http_video_thumbextractor_module_ipc_ctxs[i].processing = 1;
            slot = i;
            break;
        }
    }

    if (slot >= 0) {
        ngx_http_video_thumbextractor_start_extract(slot);
    }
}


static ngx_int_t
ngx_http_video_thumbextractor_fork_extract_process(ngx_uint_t slot)
{
    ngx_http_video_thumbextractor_ipc_t      *ipc_ctx = &ngx_http_video_thumbextractor_module_ipc_ctxs[slot];
    ngx_socket_t                             *socks = ipc_ctx->sockets;
    ngx_event_t                              *rev;
    ngx_pid_t                                 pid;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, socks) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno, "socketpair() failed on socketpair while initializing video thumb extractor module");
        return NGX_ERROR;
    }

    if (ngx_nonblocking(socks[0]) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno, ngx_nonblocking_n " failed on socketpair while initializing video thumb extractor module");
        ngx_close_channel(socks, ngx_cycle->log);
        return NGX_ERROR;
    }

    if (ngx_nonblocking(socks[1]) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno, ngx_nonblocking_n " failed on socketpair while initializing video thumb extractor module");
        ngx_close_channel(socks, ngx_cycle->log);
        return NGX_ERROR;
    }

    if (fcntl(socks[0], F_SETOWN, ngx_pid) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno, "fcntl(F_SETOWN) failed on socketpair while initializing video thumb extractor module");
        ngx_close_channel(socks, ngx_cycle->log);
        return NGX_ERROR;
    }

    if (fcntl(socks[0], F_SETFD, FD_CLOEXEC) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno, "fcntl(FD_CLOEXEC) failed on socketpair while initializing video thumb extractor module");
        ngx_close_channel(socks, ngx_cycle->log);
        return NGX_ERROR;
    }

    if (fcntl(socks[1], F_SETFD, FD_CLOEXEC) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno, "fcntl(FD_CLOEXEC) failed while initializing video thumb extractor module");
        ngx_close_channel(socks, ngx_cycle->log);
        return NGX_ERROR;
    }

    /* ignore the signal when the child dies */
    signal(SIGCHLD, SIG_IGN);

    pid = fork();

    switch (pid) {

    case -1:
        /* failure */
        ngx_close_channel(socks, ngx_cycle->log);
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno, "video thumb extractor module: unable to fork the process");
        break;

    case 0:
        /* child */

#if (NGX_LINUX)
        prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
#endif

        ngx_http_video_thumbextractor_transfer_sockets[0] = socks[0];
        ngx_http_video_thumbextractor_transfer_sockets[1] = socks[1];
        ngx_pid = ngx_getpid();
        ngx_setproctitle("thumb extractor");
        ngx_http_video_thumbextractor_run_extract(socks[1]);

        break;

    default:
        /* parent */
        ipc_ctx->pid = pid;
        ipc_ctx->transfer.conn = ngx_get_connection(socks[0], ngx_cycle->log);
        ipc_ctx->transfer.conn->data = ipc_ctx;

        rev = ipc_ctx->transfer.conn->read;
        rev->handler = ngx_http_video_thumbextractor_extract_process_parent_read_handler;

        if (ngx_add_event(rev, NGX_READ_EVENT, 0) != NGX_OK) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno, "video thumb extractor module: failed to add child control event");
        }
        break;
    }

    return NGX_OK;
}


void
ngx_http_video_thumbextractor_start_extract(ngx_uint_t slot)
{
    ngx_http_video_thumbextractor_ipc_t       *ipc_ctx = &ngx_http_video_thumbextractor_module_ipc_ctxs[slot];
    ngx_http_video_thumbextractor_transfer_t  *transfer = &ipc_ctx ->transfer;
    ngx_event_t                               *wev;
    ngx_queue_t                               *q;
    ngx_http_video_thumbextractor_ctx_t       *ctx;
    ngx_connection_t                          *c;

    q = ngx_queue_head(ngx_http_video_thumbextractor_module_extract_queue);
    ngx_queue_remove(q);
    ngx_queue_init(q);
    ctx = ngx_queue_data(q, ngx_http_video_thumbextractor_ctx_t, queue);

    ctx->slot = slot;
    ipc_ctx->request = ctx->request;

    c = transfer->conn;
    ngx_memzero(transfer, sizeof(ngx_http_video_thumbextractor_transfer_t));
    transfer->conn = c;
    transfer->thumb_ctx = &ctx->thumb_ctx;
    transfer->vtlcf = ngx_http_get_module_loc_conf(ctx->request, ngx_http_video_thumbextractor_module);

    transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_CONTEXT;
    ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, (u_char *) transfer->thumb_ctx, NULL, sizeof(ngx_http_video_thumbextractor_thumb_ctx_t));

    wev = ipc_ctx->transfer.conn->write;
    wev->handler = ngx_http_video_thumbextractor_send_values_to_process;

    if (ngx_add_event(wev, NGX_WRITE_EVENT, 0) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno, "video thumb extractor module: failed to add parent write event");
    }

    ngx_http_video_thumbextractor_send_values_to_process(wev);
}


void
ngx_http_video_thumbextractor_send_values_to_process(ngx_event_t *ev)
{
    ngx_http_video_thumbextractor_ipc_t       *ipc_ctx;
    ngx_http_video_thumbextractor_transfer_t  *transfer;
    ngx_http_video_thumbextractor_thumb_ctx_t *thumb_ctx;
    ngx_connection_t                          *c;
    ngx_http_request_t                        *r;
    ngx_int_t                                  rc;

    c = ev->data;
    ipc_ctx = c->data;
    transfer = &ipc_ctx->transfer;
    thumb_ctx = transfer->thumb_ctx;

    r = ipc_ctx->request;

    if (r == NULL) {
        ngx_log_debug(NGX_LOG_DEBUG, ngx_cycle->log, 0, "video thumb extractor module: request already gone");
        goto request_gone;
    }

    ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, transfer->buffer.start, transfer->buffer.last, 0);

    if ((rc = ngx_http_video_thumbextractor_write(c, ev, &transfer->buffer, transfer->buffer.end - transfer->buffer.start)) != NGX_OK) {
        goto transfer_failed;
    }

    switch (transfer->step) {
    case NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_CONTEXT:
        ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, thumb_ctx->filename.data, NULL, thumb_ctx->filename.len);
        transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_FILENAME;
        break;

    case NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_FILENAME:
        ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, (u_char *) &transfer->vtlcf, NULL, sizeof(ngx_http_video_thumbextractor_loc_conf_t *));
        transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_CONFIG;
        break;

    case NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_CONFIG:
        ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, (u_char *) &transfer->rc, NULL, sizeof(ngx_int_t));
        transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_RC;
        goto exit;
        break;

    default:
        break;
    }

    return;

transfer_failed:

    if (rc == NGX_AGAIN) {
        return;
    }

    if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0, "video thumb extractor module: error sending data to extract thumbor process");
        ngx_http_filter_finalize_request(r, &ngx_http_video_thumbextractor_module, NGX_HTTP_INTERNAL_SERVER_ERROR);
        ngx_http_finalize_request(r, NGX_OK);
    }

request_gone:

    ngx_http_video_thumbextractor_cleanup_parent_process(ipc_ctx);

exit:

    if (ngx_del_event(ev, NGX_WRITE_EVENT, 0) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno, "video thumb extractor module: failed to remove parent write event");
    }
}


void
ngx_http_video_thumbextractor_run_extract(ngx_int_t fd)
{
    ngx_event_t                               *rev;
    ngx_log_t                                 *log;
    ngx_cycle_t                               *cycle;
    ngx_pool_t                                *pool;
    ngx_int_t                                  i;

    ngx_done_events((ngx_cycle_t *) ngx_cycle);

    if (signal(SIGTERM, ngx_http_video_thumbextractor_sig_handler) == SIG_ERR) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno, "video thumb extractor module: could not set the catch signal for SIGTERM");
    }

    if (signal(SIGINT, ngx_http_video_thumbextractor_sig_handler) == SIG_ERR) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno, "video thumb extractor module: could not set the catch signal for SIGINT");
    }

    if (signal(SIGUSR1, ngx_http_video_thumbextractor_sig_handler) == SIG_ERR) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno, "video thumb extractor module: could not set the catch signal for SIGUSR1");
    }

    log = ngx_cycle->log;

    pool = ngx_create_pool(NGX_CYCLE_POOL_SIZE, log);
    if (pool == NULL) {
        exit(1);
    }
    pool->log = log;

    cycle = ngx_pcalloc(pool, sizeof(ngx_cycle_t));
    if (cycle == NULL) {
        ngx_destroy_pool(pool);
        exit(1);
    }

    cycle->pool = pool;
    cycle->log = log;
    cycle->new_log.log_level = NGX_LOG_ERR;
    cycle->old_cycle = (ngx_cycle_t *) ngx_cycle;
    cycle->conf_ctx = ngx_cycle->conf_ctx;
    cycle->conf_file = ngx_cycle->conf_file;
    cycle->conf_param = ngx_cycle->conf_param;
    cycle->conf_prefix = ngx_cycle->conf_prefix;

    cycle->connection_n = 16;

    ngx_process = NGX_PROCESS_HELPER;

    for (i = 0; ngx_modules[i]; i++) {
        if ((ngx_modules[i]->type == NGX_EVENT_MODULE) && ngx_modules[i]->init_process) {
            if (ngx_modules[i]->init_process(cycle) == NGX_ERROR) {
                /* fatal */
                exit(2);
            }
        }
    }

    ngx_close_listening_sockets(cycle);

    ngx_cycle = cycle;

    ngx_http_video_thumbextractor_transfer.pool = NULL;
    ngx_http_video_thumbextractor_transfer.conn = ngx_get_connection(fd, ngx_cycle->log);

    rev = ngx_http_video_thumbextractor_transfer.conn->read;
    rev->handler = ngx_http_video_thumbextractor_extract_process_child_read_handler;

    if (ngx_add_event(rev, NGX_READ_EVENT, 0) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno, "video thumb extractor module: failed to add child read event");
    }

    for ( ;; ) {
        if (ngx_exiting || ngx_quit) {
            exit(0);
        }

        ngx_process_events_and_timers(cycle);
    }
}


void
ngx_http_video_thumbextractor_extract_process_parent_read_handler(ngx_event_t *ev)
{
    ngx_http_video_thumbextractor_ipc_t       *ipc_ctx;
    ngx_http_video_thumbextractor_transfer_t  *transfer;
    ngx_connection_t                          *c;
    ngx_http_request_t                        *r;
    ngx_chain_t                               *out;
    ngx_int_t                                  rc;

    c = ev->data;
    ipc_ctx = c->data;
    transfer = &ipc_ctx->transfer;

    r = ipc_ctx->request;

    if (r == NULL) {
        ngx_log_debug(NGX_LOG_DEBUG, ngx_cycle->log, 0, "video thumb extractor module: request already gone");
        goto request_gone;
    }

    ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, transfer->buffer.start, transfer->buffer.last, 0);

    if ((rc = ngx_http_video_thumbextractor_recv(c, ev, &transfer->buffer, transfer->buffer.end - transfer->buffer.start)) != NGX_OK) {
        goto transfer_failed;
    }

    switch (transfer->step) {
    case NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_RC:
        transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_FINISHED;

        if (transfer->rc == NGX_ERROR) {
            ngx_http_filter_finalize_request(r, &ngx_http_video_thumbextractor_module, NGX_HTTP_INTERNAL_SERVER_ERROR);
            goto exit;
        }

        if ((transfer->rc == NGX_HTTP_VIDEO_THUMBEXTRACTOR_FILE_NOT_FOUND) || (transfer->rc == NGX_HTTP_VIDEO_THUMBEXTRACTOR_SECOND_NOT_FOUND)) {
            ngx_http_filter_finalize_request(r, &ngx_http_video_thumbextractor_module, NGX_HTTP_NOT_FOUND);
            goto exit;
        }

        ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, (u_char *) &transfer->size, NULL, sizeof(size_t));
        transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_IMAGE_LEN;
        break;

    case NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_IMAGE_LEN:
        if ((transfer->buffer.start = ngx_pcalloc(r->pool, transfer->size)) == NULL) {
            ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0, "video thumb extractor module: unable to allocate buffer to receive the image");
            ngx_http_filter_finalize_request(r, &ngx_http_video_thumbextractor_module, NGX_HTTP_INTERNAL_SERVER_ERROR);
            goto exit;
        }
        transfer->buffer.temporary = 1;

        ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, transfer->buffer.start, NULL, transfer->size);
        transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_IMAGE_DATA;
        break;

    case NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_IMAGE_DATA:
        /* write response */
        r->headers_out.content_type = NGX_HTTP_VIDEO_THUMBEXTRACTOR_CONTENT_TYPE;
        r->headers_out.content_type_len = NGX_HTTP_VIDEO_THUMBEXTRACTOR_CONTENT_TYPE.len;
        r->headers_out.status = NGX_HTTP_OK;
        r->headers_out.content_length_n = transfer->size;

        out = (ngx_chain_t *) ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
        if (out == NULL) {
            ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0, "video thumb extractor module: unable to allocate output to send the image");
            ngx_http_filter_finalize_request(r, &ngx_http_video_thumbextractor_module, NGX_HTTP_INTERNAL_SERVER_ERROR);
            goto exit;
        }

        transfer->buffer.last_buf = 1;
        transfer->buffer.last_in_chain = 1;
        transfer->buffer.flush = 1;
        transfer->buffer.memory = 1;

        out->buf = &transfer->buffer;
        out->next = NULL;

        rc = ngx_http_video_thumbextractor_next_header_filter(r);
        if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
            goto exit;
        }
        ngx_http_video_thumbextractor_next_body_filter(r, out);

        goto exit;
        break;

    default:
        break;
    }

    return;

transfer_failed:

    if (rc == NGX_AGAIN) {
        return;
    }

    if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0, "video thumb extractor module: error receiving data from extract thumbor process");
        ngx_http_filter_finalize_request(r, &ngx_http_video_thumbextractor_module, NGX_HTTP_INTERNAL_SERVER_ERROR);
    }

request_gone:

    ngx_http_video_thumbextractor_cleanup_parent_process(ipc_ctx);

exit:

    ipc_ctx->processing = 0;
    ipc_ctx->request = NULL;

    if (r != NULL) {
        ngx_http_finalize_request(r, NGX_OK);
    }

    ngx_http_video_thumbextractor_module_ensure_extractor_process();
}


void
ngx_http_video_thumbextractor_extract_process_child_read_handler(ngx_event_t *ev)
{
    ngx_http_video_thumbextractor_transfer_t  *transfer = &ngx_http_video_thumbextractor_transfer;
    ngx_http_video_thumbextractor_thumb_ctx_t *thumb_ctx = transfer->thumb_ctx;
    ngx_connection_t                          *c;
    ngx_int_t                                  rc;
    ngx_event_t                               *wev;

    c = ev->data;

    if (transfer->pool == NULL) {
        if ((transfer->pool = ngx_create_pool(4096, ngx_cycle->log)) == NULL) {
            ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, 0, "video thumb extractor module: unable to allocate temporary pool to extract the thumb");
            goto allocation_failed;
        }

        if ((transfer->thumb_ctx = ngx_pcalloc(transfer->pool, sizeof(ngx_http_video_thumbextractor_thumb_ctx_t))) == NULL) {
            ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, 0, "video thumb extractor module: unable to allocate temporary thumb context");
            goto allocation_failed;
        }

        thumb_ctx = transfer->thumb_ctx;
        transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_CONTEXT;
        ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, (u_char *) thumb_ctx, NULL, sizeof(ngx_http_video_thumbextractor_thumb_ctx_t));
    }

    ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, transfer->buffer.start, transfer->buffer.last, 0);

    if ((rc = ngx_http_video_thumbextractor_recv(c, ev, &transfer->buffer, transfer->buffer.end - transfer->buffer.start)) != NGX_OK) {
        goto transfer_failed;
    }

    switch (transfer->step) {
    case NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_CONTEXT:
        if ((thumb_ctx->filename.data = ngx_pcalloc(transfer->pool, thumb_ctx->filename.len + 1)) == NULL) {
            ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, 0, "video thumb extractor module: unable to allocate temporary pool to extract the thumb");
            goto allocation_failed;
        }
        thumb_ctx->filename.data[thumb_ctx->filename.len] = '\0';

        ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, thumb_ctx->filename.data, NULL, thumb_ctx->filename.len);
        transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_FILENAME;
        break;

    case NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_FILENAME:
        ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, (u_char *) &transfer->vtlcf, NULL, sizeof(ngx_http_video_thumbextractor_loc_conf_t *));
        transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_CONFIG;
        break;

    case NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_CONFIG:
        transfer->data = 0;
        transfer->size = 0;

        transfer->rc = ngx_http_video_thumbextractor_get_thumb(transfer->vtlcf, thumb_ctx, &transfer->data, &transfer->size, transfer->pool, ngx_cycle->log);

        transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_RC;
        ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, (u_char *) &transfer->rc, NULL, sizeof(ngx_int_t));

        wev = c->write;
        wev->handler = ngx_http_video_thumbextractor_extract_process_write_handler;

        if (ngx_add_event(wev, NGX_WRITE_EVENT, 0) != NGX_OK) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno, "video thumb extractor module: failed to add child write event");
        }

        ngx_http_video_thumbextractor_extract_process_write_handler(wev);

        break;

    default:
        break;
    }

    return;

transfer_failed:

    if (rc == NGX_AGAIN) {
        return;
    }

    if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, ngx_errno, "video thumb extractor module: Could not read data from parent !!!");
    }

allocation_failed:

    rc = NGX_ERROR;
    ngx_write_fd(c->fd, &rc, sizeof(ngx_int_t));
    ngx_http_video_thumbextractor_cleanup_extract_process(transfer);
}


void
ngx_http_video_thumbextractor_extract_process_write_handler(ngx_event_t *ev)
{
    ngx_http_video_thumbextractor_transfer_t  *transfer = &ngx_http_video_thumbextractor_transfer;
    ngx_connection_t                          *c;
    ngx_int_t                                  rc;

    c = ev->data;

    ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, transfer->buffer.start, transfer->buffer.last, 0);

    if ((rc = ngx_http_video_thumbextractor_write(c, ev, &transfer->buffer, transfer->buffer.end - transfer->buffer.start)) != NGX_OK) {
        goto transfer_failed;
    }

    switch (transfer->step) {
    case NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_RC:
        if (transfer->rc == NGX_OK) {
            ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, (u_char *) &transfer->size, NULL, sizeof(size_t));
            transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_IMAGE_LEN;
        } else {
            transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_FINISHED;
        }
        break;

    case NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_IMAGE_LEN:
        ngx_http_video_thumbextractor_set_buffer(&transfer->buffer, (u_char *) transfer->data, NULL, transfer->size);
        transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_IMAGE_DATA;
        break;

    case NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_IMAGE_DATA:
    case NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_FINISHED:
        goto exit;
        break;

    default:
        break;
    }

    return;

transfer_failed:

    if (rc == NGX_AGAIN) {
        return;
    }

exit:

    if (ngx_del_event(ev, NGX_WRITE_EVENT, 0) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno, "video thumb extractor module: failed to remove parent write event");
    }

    ngx_http_video_thumbextractor_cleanup_extract_process(transfer);
}


void
ngx_http_video_thumbextractor_cleanup_parent_process(ngx_http_video_thumbextractor_ipc_t *ipc_ctx)
{
    ngx_http_video_thumbextractor_transfer_t  *transfer = &ipc_ctx->transfer;
    ngx_event_t                               *wev;
    u_char                                     trash[1];

    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "video thumb extractor module: FORCED CLEANUP, SENDING SIGUSR1");

    // remove write event
    if (transfer->conn != NULL) {
        wev = transfer->conn->write;
        if (wev->active) {
            if (ngx_del_event(wev, NGX_WRITE_EVENT, 0) != NGX_OK) {
                ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno, "video thumb extractor module: failed to remove parent write event");
            }
        }
    }

    kill(ipc_ctx->pid, SIGUSR1);

    // read all pending data
    while(ngx_read_fd(transfer->conn->fd, trash, 1) == 1) { /* just to flush the socket */ }

    ipc_ctx->processing = 0;
    ipc_ctx->request = NULL;
}


void
ngx_http_video_thumbextractor_cleanup_extract_process(ngx_http_video_thumbextractor_transfer_t *transfer)
{
    ngx_event_t                               *wev;

    // remove write event
    if (transfer->conn != NULL) {
        wev = transfer->conn->write;
        if (wev->active) {
            if (ngx_del_event(wev, NGX_WRITE_EVENT, 0) != NGX_OK) {
                ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno, "video thumb extractor module: failed to remove parent write event");
            }
        }
    }

    if (transfer->pool != NULL) {
        ngx_destroy_pool(transfer->pool);
        transfer->pool = NULL;
        transfer->step = NGX_HTTP_VIDEO_THUMBEXTRACTOR_TRANSFER_FINISHED;
    }
}


ngx_int_t
ngx_http_video_thumbextractor_recv(ngx_connection_t *c, ngx_event_t *rev, ngx_buf_t *buf, ssize_t len)
{
    ssize_t size = len - (buf->last - buf->start);
    if (size == 0) {
        return NGX_OK;
    }

    ssize_t n = ngx_read_fd(c->fd, buf->last, size);

    if (n == NGX_AGAIN) {
        return NGX_AGAIN;
    }

    if ((n == NGX_ERROR) || (n == 0)) {
        return NGX_ERROR;
    }

    buf->last += n;

    if ((buf->last - buf->start) < len) {
        return NGX_AGAIN;
    }

    return NGX_OK;
}


ngx_int_t
ngx_http_video_thumbextractor_write(ngx_connection_t *c, ngx_event_t *wev, ngx_buf_t *buf, ssize_t len)
{
    ssize_t size = len - (buf->last - buf->start);
    if (size == 0) {
        return NGX_OK;
    }

    ssize_t n = ngx_write_fd(c->fd, buf->last, size);

    if (n == NGX_AGAIN) {
        return NGX_AGAIN;
    }

    if ((n == NGX_ERROR) || (n == 0)) {
        return NGX_ERROR;
    }

    buf->last += n;

    if ((buf->last - buf->start) < len) {
        return NGX_AGAIN;
    }

    return NGX_OK;
}


void
ngx_http_video_thumbextractor_set_buffer(ngx_buf_t *buf, u_char *start, u_char *last, ssize_t len)
{
    buf->start = start;
    buf->pos = buf->start;
    buf->last = (last != NULL) ? last : start;
    buf->end = len ? buf->start + len : buf->end;
    buf->temporary = 0;
    buf->memory = 1;
}


void
ngx_http_video_thumbextractor_sig_handler(int signo)
{
    ngx_http_video_thumbextractor_transfer_t  *transfer = &ngx_http_video_thumbextractor_transfer;
    u_char                                     trash[1];

    if ((signo == SIGTERM) || (signo == SIGINT)) {
        ngx_quit = 1;
        ngx_http_video_thumbextractor_cleanup_extract_process(transfer);
        ngx_close_socket(ngx_http_video_thumbextractor_transfer_sockets[0]);
        ngx_close_socket(ngx_http_video_thumbextractor_transfer_sockets[1]);
    }

    if (signo == SIGTERM) {
        ngx_done_events((ngx_cycle_t *) ngx_cycle);
    }

    if (signo == SIGUSR1) {
        // reset state machine
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "video thumb extractor module: received SIGUSR1");

        // read all pending data
        while(ngx_read_fd(transfer->conn->fd, trash, 1) == 1) { /* just to flush the socket */ }

        // clear transfer structure
        ngx_http_video_thumbextractor_cleanup_extract_process(transfer);
    }
}
