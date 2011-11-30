#include <ngx_http_video_thumbextractor_module_utils.h>
#include <ngx_http_video_thumbextractor_module.h>

static void *ngx_http_video_thumbextractor_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_video_thumbextractor_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static ngx_int_t ngx_http_video_thumbextractor_init_worker(ngx_cycle_t *cycle);

static char *ngx_http_video_thumbextractor(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

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
      ngx_null_command
};

static ngx_http_module_t  ngx_http_video_thumbextractor_module_ctx = {
    NULL,                                           /* preconfiguration */
    NULL,                                           /* postconfiguration */

    NULL,                                           /* create main configuration */
    NULL,                                           /* init main configuration */

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
    NULL,                                        /* exit process */
    NULL,                                        /* exit master */
    NGX_MODULE_V1_PADDING
};


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
    conf->jpeg_baseline = NGX_CONF_UNSET_UINT;
    conf->jpeg_progressive_mode = NGX_CONF_UNSET_UINT;
    conf->jpeg_optimize = NGX_CONF_UNSET_UINT;
    conf->jpeg_smooth = NGX_CONF_UNSET_UINT;
    conf->jpeg_quality = NGX_CONF_UNSET_UINT;
    conf->jpeg_dpi = NGX_CONF_UNSET_UINT;

    return conf;
}


static char *
ngx_http_video_thumbextractor_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_video_thumbextractor_loc_conf_t *prev = parent;
    ngx_http_video_thumbextractor_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->enabled, prev->enabled, 0);

    // if video thumb extractor is disable the other configurations don't have to be checked
    if (!conf->enabled) {
        return NGX_CONF_OK;
    }

    if (conf->video_filename == NULL) {
        conf->video_filename = prev->video_filename;
    }

    if (conf->video_second == NULL) {
        conf->video_second = prev->video_second;
    }

    if (conf->image_width == NULL) {
        conf->image_width = prev->image_width;
    }

    if (conf->image_height == NULL) {
        conf->image_height = prev->image_height;
    }

    ngx_conf_merge_uint_value(conf->jpeg_baseline, prev->jpeg_baseline, 1);
    ngx_conf_merge_uint_value(conf->jpeg_progressive_mode, prev->jpeg_progressive_mode, 0);
    ngx_conf_merge_uint_value(conf->jpeg_optimize, prev->jpeg_optimize, 100);
    ngx_conf_merge_uint_value(conf->jpeg_smooth, prev->jpeg_smooth, 0);
    ngx_conf_merge_uint_value(conf->jpeg_quality, prev->jpeg_quality, 75);
    ngx_conf_merge_uint_value(conf->jpeg_dpi, prev->jpeg_dpi, 72); /** Screen resolution = 72 dpi */

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
ngx_http_video_thumbextractor_init_worker(ngx_cycle_t *cycle)
{
    ngx_http_video_thumbextractor_init_libraries();
    return NGX_OK;
}


static char *
ngx_http_video_thumbextractor(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t                 *clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    ngx_http_video_thumbextractor_loc_conf_t *vtlcf = conf;

    clcf->handler = ngx_http_video_thumbextractor_handler;

    vtlcf->enabled = 1;

    return NGX_CONF_OK;
}

