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
 * ngx_http_video_thumbextractor_module_setup.c
 *
 * Created:  Nov 22, 2011
 * Author:   Wandenberg Peixoto <wandenberg@gmail.com>
 *
 */
#include <ngx_http_video_thumbextractor_module_utils.h>
#include <ngx_http_video_thumbextractor_module_ipc.h>
#include <ngx_http_video_thumbextractor_module.h>

static void *ngx_http_video_thumbextractor_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_video_thumbextractor_init_main_conf(ngx_conf_t *cf, void *parent);

static void *ngx_http_video_thumbextractor_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_video_thumbextractor_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static ngx_int_t ngx_http_video_thumbextractor_post_config(ngx_conf_t *cf);
static ngx_int_t ngx_http_video_thumbextractor_init_worker(ngx_cycle_t *cycle);
static void      ngx_http_video_thumbextractor_exit_worker(ngx_cycle_t *cycle);

static char *ngx_http_video_thumbextractor(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

ngx_flag_t ngx_http_video_thumbextractor_used = 0;

#define ngx_conf_merge_null_value(conf, prev, default)             \
    if (conf == NULL) {                                            \
        conf = (prev == NULL) ? default : prev;                    \
    }

static ngx_command_t  ngx_http_video_thumbextractor_commands[] = {
    { ngx_string("video_thumbextractor"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_video_thumbextractor,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },
    { ngx_string("video_thumbextractor_video_filename"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, video_filename),
      NULL },
    { ngx_string("video_thumbextractor_video_second"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, video_second),
      NULL },
    { ngx_string("video_thumbextractor_only_keyframe"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, only_keyframe),
      NULL },
    { ngx_string("video_thumbextractor_next_time"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, next_time),
      NULL },
    { ngx_string("video_thumbextractor_tile_vtt"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, tile_vtt),
      NULL },
    { ngx_string("video_thumbextractor_image_width"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, image_width),
      NULL },
    { ngx_string("video_thumbextractor_image_height"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, image_height),
      NULL },
    { ngx_string("video_thumbextractor_tile_rows"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, tile_rows),
      NULL },
    { ngx_string("video_thumbextractor_tile_max_rows"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, tile_max_rows),
      NULL },
    { ngx_string("video_thumbextractor_tile_cols"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, tile_cols),
      NULL },
    { ngx_string("video_thumbextractor_tile_max_cols"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, tile_max_cols),
      NULL },
    { ngx_string("video_thumbextractor_tile_sample_interval"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, tile_sample_interval),
      NULL },
    { ngx_string("video_thumbextractor_tile_margin"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, tile_margin),
      NULL },
    { ngx_string("video_thumbextractor_tile_padding"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, tile_padding),
      NULL },
    { ngx_string("video_thumbextractor_tile_color"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, tile_color),
      NULL },
    { ngx_string("video_thumbextractor_jpeg_baseline"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, jpeg_baseline),
      NULL },
    { ngx_string("video_thumbextractor_jpeg_progressive_mode"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, jpeg_progressive_mode),
      NULL },
    { ngx_string("video_thumbextractor_jpeg_optimize"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, jpeg_optimize),
      NULL },
    { ngx_string("video_thumbextractor_jpeg_smooth"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, jpeg_smooth),
      NULL },
    { ngx_string("video_thumbextractor_jpeg_quality"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, jpeg_quality),
      NULL },
    { ngx_string("video_thumbextractor_jpeg_dpi"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, jpeg_dpi),
      NULL },
    { ngx_string("video_thumbextractor_threads"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_loc_conf_t, threads),
      NULL },
    { ngx_string("video_thumbextractor_processes_per_worker"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_video_thumbextractor_main_conf_t, processes_per_worker),
      NULL },
      ngx_null_command
};

static ngx_http_module_t  ngx_http_video_thumbextractor_module_ctx = {
    NULL,                                           /* preconfiguration */
    ngx_http_video_thumbextractor_post_config,      /* postconfiguration */

    ngx_http_video_thumbextractor_create_main_conf, /* create main configuration */
    ngx_http_video_thumbextractor_init_main_conf,   /* init main configuration */

    NULL,                                           /* create server configuration */
    NULL,                                           /* merge server configuration */

    ngx_http_video_thumbextractor_create_loc_conf,  /* create location configration */
    ngx_http_video_thumbextractor_merge_loc_conf    /* merge location configration */
};


ngx_module_t  ngx_http_video_thumbextractor_module = {
    NGX_MODULE_V1,
    &ngx_http_video_thumbextractor_module_ctx,   /* module context */
    ngx_http_video_thumbextractor_commands,      /* module directives */
    NGX_HTTP_MODULE,                             /* module type */
    NULL,                                        /* init master */
    NULL,                                        /* init module */
    ngx_http_video_thumbextractor_init_worker,   /* init process */
    NULL,                                        /* init thread */
    NULL,                                        /* exit thread */
    ngx_http_video_thumbextractor_exit_worker,   /* exit process */
    NULL,                                        /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_http_video_thumbextractor_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_video_thumbextractor_main_conf_t    *mcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_video_thumbextractor_main_conf_t));

    if (mcf == NULL) {
        return NGX_CONF_ERROR;
    }

    mcf->processes_per_worker = NGX_CONF_UNSET_UINT;

    return mcf;
}


static char *
ngx_http_video_thumbextractor_init_main_conf(ngx_conf_t *cf, void *parent)
{

    ngx_http_video_thumbextractor_main_conf_t     *conf = parent;

    ngx_conf_merge_uint_value(conf->processes_per_worker, NGX_CONF_UNSET_UINT, 1);

    if (conf->processes_per_worker > NGX_MAX_PROCESSES) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "video thumbextractor module: video_thumbextractor_processes_per_worker must be less than %d", NGX_MAX_PROCESSES);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static void *
ngx_http_video_thumbextractor_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_video_thumbextractor_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_video_thumbextractor_loc_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }

    conf->enabled = NGX_CONF_UNSET;
    conf->video_filename = NULL;
    conf->video_second = NULL;
    conf->image_width = NULL;
    conf->image_height = NULL;
    conf->tile_sample_interval = NULL;
    conf->tile_cols = NULL;
    conf->tile_max_cols = NULL;
    conf->tile_rows = NULL;
    conf->tile_max_rows = NULL;
    conf->tile_margin = NULL;
    conf->tile_padding = NULL;
    ngx_str_null(&conf->tile_color);
    conf->jpeg_baseline = NGX_CONF_UNSET_UINT;
    conf->jpeg_progressive_mode = NGX_CONF_UNSET_UINT;
    conf->jpeg_optimize = NGX_CONF_UNSET_UINT;
    conf->jpeg_smooth = NGX_CONF_UNSET_UINT;
    conf->jpeg_quality = NGX_CONF_UNSET_UINT;
    conf->jpeg_dpi = NGX_CONF_UNSET_UINT;
    conf->only_keyframe = NGX_CONF_UNSET_UINT;
    conf->next_time = NGX_CONF_UNSET_UINT;
    conf->tile_vtt = NGX_CONF_UNSET_UINT;
    ngx_str_null(&conf->threads);

    return conf;
}


static char *
ngx_http_video_thumbextractor_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_video_thumbextractor_loc_conf_t *prev = parent;
    ngx_http_video_thumbextractor_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->enabled, prev->enabled, 0);

    ngx_conf_merge_null_value(conf->video_filename, prev->video_filename, NULL);
    ngx_conf_merge_null_value(conf->video_second, prev->video_second, NULL);
    ngx_conf_merge_null_value(conf->image_width, prev->image_width, NULL);
    ngx_conf_merge_null_value(conf->image_height, prev->image_height, NULL);
    ngx_conf_merge_null_value(conf->tile_sample_interval, prev->tile_sample_interval, NULL);
    ngx_conf_merge_null_value(conf->tile_cols, prev->tile_cols, NULL);
    ngx_conf_merge_null_value(conf->tile_max_cols, prev->tile_max_cols, NULL);
    ngx_conf_merge_null_value(conf->tile_rows, prev->tile_rows, NULL);
    ngx_conf_merge_null_value(conf->tile_max_rows, prev->tile_max_rows, NULL);
    ngx_conf_merge_null_value(conf->tile_margin, prev->tile_margin, NULL);
    ngx_conf_merge_null_value(conf->tile_padding, prev->tile_padding, NULL);
    ngx_conf_merge_str_value(conf->tile_color, prev->tile_color, "black");

    ngx_conf_merge_uint_value(conf->jpeg_baseline, prev->jpeg_baseline, 1);
    ngx_conf_merge_uint_value(conf->jpeg_progressive_mode, prev->jpeg_progressive_mode, 0);
    ngx_conf_merge_uint_value(conf->jpeg_optimize, prev->jpeg_optimize, 100);
    ngx_conf_merge_uint_value(conf->jpeg_smooth, prev->jpeg_smooth, 0);
    ngx_conf_merge_uint_value(conf->jpeg_quality, prev->jpeg_quality, 75);
    ngx_conf_merge_uint_value(conf->jpeg_dpi, prev->jpeg_dpi, 72); /** Screen resolution = 72 dpi */

    ngx_conf_merge_value(conf->only_keyframe, prev->only_keyframe, 1);
    ngx_conf_merge_value(conf->next_time, prev->next_time, 1);
    ngx_conf_merge_value(conf->tile_vtt, prev->tile_vtt, 0);

    ngx_conf_merge_str_value(conf->threads, prev->threads, "auto");

    // if video thumb extractor is disable the other configurations don't have to be checked
    if (!conf->enabled) {
        return NGX_CONF_OK;
    }

    conf->next_time = conf->only_keyframe ? conf->next_time : 0;

    // sanity checks

    if (conf->video_filename == NULL) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "video thumbextractor module: video_thumbextractor_video_filename must be defined");
        return NGX_CONF_ERROR;
    }

    if (conf->video_second == NULL) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "video thumbextractor module: video_thumbextractor_video_second must be defined");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_video_thumbextractor_post_config(ngx_conf_t *cf)
{
    ngx_int_t                        rc;

    if (!ngx_http_video_thumbextractor_used) {
        return NGX_OK;
    }

    /* register our output filters */
    if ((rc = ngx_http_video_thumbextractor_filter_init(cf)) != NGX_OK) {
        return rc;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_video_thumbextractor_init_worker(ngx_cycle_t *cycle)
{
    ngx_uint_t i;

    for (i = 0; i < NGX_MAX_PROCESSES; ++i) {
        ngx_http_video_thumbextractor_module_ipc_ctxs[i].pid = -1;
        ngx_http_video_thumbextractor_module_ipc_ctxs[i].slot = i;
    }

    if ((ngx_http_video_thumbextractor_module_extract_queue = ngx_pcalloc(ngx_cycle->pool, sizeof(ngx_queue_t))) == NULL) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "video thumb extractor module: unable to allocate memory to queue of extractions");
        return NGX_ERROR;
    }

    ngx_queue_init(ngx_http_video_thumbextractor_module_extract_queue);

    ngx_http_video_thumbextractor_init_libraries();
    return NGX_OK;
}


static void
ngx_http_video_thumbextractor_exit_worker(ngx_cycle_t *cycle)
{
    ngx_uint_t                                 i;

    for (i = 0; i < NGX_MAX_PROCESSES; i++) {
        if (ngx_http_video_thumbextractor_module_ipc_ctxs[i].pid != -1) {
            ngx_close_socket(ngx_http_video_thumbextractor_module_ipc_ctxs[i].pipefd[0]);
            ngx_close_socket(ngx_http_video_thumbextractor_module_ipc_ctxs[i].pipefd[1]);
            kill(ngx_http_video_thumbextractor_module_ipc_ctxs[i].pid, SIGTERM);
        }
    }
}



static char *
ngx_http_video_thumbextractor(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_video_thumbextractor_loc_conf_t *vtlcf = conf;

    vtlcf->enabled = 1;
    ngx_http_video_thumbextractor_used = 1;

    return NGX_CONF_OK;
}

