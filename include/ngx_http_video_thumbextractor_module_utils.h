#ifndef NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_UTILS_H_
#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_UTILS_H_

static ngx_str_t                              *ngx_http_video_thumbextractor_create_str(ngx_pool_t *pool, uint len);

static int                                     ngx_http_video_thumbextractor_get_thumb(ngx_http_video_thumbextractor_loc_conf_t *cf, const char *filename, int64_t second, ngx_uint_t width, ngx_uint_t height, caddr_t *out_buffer, size_t *out_len, ngx_pool_t *temp_pool, ngx_log_t *log);
static void                                    ngx_http_video_thumbextractor_init_libraries(void);

#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_FILE_NOT_FOUND   1
#define NGX_HTTP_VIDEO_THUMBEXTRACTOR_SECOND_NOT_FOUND 2

#endif /* NGX_HTTP_VIDEO_THUMBEXTRACTOR_MODULE_UTILS_H_ */
