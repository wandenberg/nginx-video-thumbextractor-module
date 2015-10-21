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
#ifndef NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_IPC_H_
#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_IPC_H_

#include <ngx_http_video_thumbextractor_module.h>

typedef struct {
    ngx_socket_t                                    pipefd[2];
    ngx_connection_t                               *conn;
    ngx_pid_t                                       pid;
    ngx_flag_t                                      processing;
    ngx_http_request_t                             *request;
    ngx_int_t                                       slot;
} ngx_http_video_thumbextractor_ipc_t;


ngx_queue_t    *ngx_http_video_thumbextractor_module_extract_queue;

ngx_http_video_thumbextractor_ipc_t    ngx_http_video_thumbextractor_module_ipc_ctxs[NGX_MAX_PROCESSES];

void            ngx_http_video_thumbextractor_module_ensure_extractor_process(void);

#endif /* NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_IPC_H_ */
