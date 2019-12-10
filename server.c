#include <stdio.h>
#include <stdlib.h>
#include <evhttp.h>
#include <event.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include "event2/http.h"
#include "event2/event.h"
#include "event2/buffer.h"
#include "event2/bufferevent.h"
#include "event2/bufferevent_compat.h"
#include "event2/http_struct.h"
#include "event2/http_compat.h"
#include "event2/util.h"
#include "event2/listener.h"
#include "event2/bufferevent_ssl.h"

#define WEB_PATH "www"
#define SERVER_CRT "server.crt"
#define SERVER_KEY "server.key"
#define HTTP_SERVER_PORT 8000
#define HTTPS_SERVER_PORT 4430
#define MAX_BUF_SIZE 1024

SSL_CTX *evssl_init(void);
struct bufferevent* bevcb (struct event_base *, void *);
void accept_request(struct evhttp_request *, void *);
char *get_content_type(char *);
void http_startup(void);
void https_startup(void);
void serve_file(struct evhttp_request *, const char *);
void handle_get_request(struct evhttp_request *, void *);
void handle_post_request(struct evhttp_request *, void *);
void handle_head_request(struct evhttp_request *, void *);
void handle_put_request(struct evhttp_request *, void *);
void handle_delete_request(struct evhttp_request *, void *);
void handle_options_request(struct evhttp_request *, void *);
void handle_trace_request(struct evhttp_request *, void *);
void handle_connect_request(struct evhttp_request *, void *);
void handle_patch_request(struct evhttp_request *, void *);
void handle_unknown_request(struct evhttp_request *, void *);

struct table_entry
{
    const char *extension;
    const char *content_type;
} content_type_table[] = {
    {"txt", "text/plain"},
    {"c", "text/plain"},
    {"h", "text/plain"},
    {"html", "text/html"},
    {"htm", "text/htm"},
    {"css", "text/css"},
    {"gif", "image/gif"},
    {"jpg", "image/jpeg"},
    {"jpeg", "image/jpeg"},
    {"js", "text/javascript"},
    {"png", "image/png"},
    {"pdf", "application/pdf"},
    {"ps", "application/postscript"},
    {NULL, NULL},
};

/* 初始化SSL */
SSL_CTX *evssl_init(void)
{
    SSL_CTX *server_ctx;
    SSL_load_error_strings();
    SSL_library_init(); // 初始化OpenSSL库
    if (!RAND_poll())
        return NULL;
    server_ctx = SSL_CTX_new(SSLv23_server_method());
    if (!SSL_CTX_use_certificate_chain_file(server_ctx, SERVER_CRT) ||
        !SSL_CTX_use_PrivateKey_file(server_ctx, SERVER_KEY, SSL_FILETYPE_PEM))
    {
        puts("Couldn't read 'server.key' or 'server.crt' file.  To generate a key\n"
             "To generate a key and certificate, run:\n"
             "  openssl genrsa -out server.key 2048\n"
             "  openssl req -new -key server.key -out server.crt.req\n"
             "  openssl x509 -req -days 365 -in server.crt.req -signkey server.key -out server.crt");
        return NULL;
    }
    SSL_CTX_set_options(server_ctx, SSL_OP_NO_SSLv2);
    return server_ctx;
}

struct bufferevent* bevcb (struct event_base *base, void *arg)
{
	SSL_CTX *ctx = (SSL_CTX *) arg;
	return bufferevent_openssl_socket_new (base, -1, SSL_new(ctx), BUFFEREVENT_SSL_ACCEPTING, BEV_OPT_CLOSE_ON_FREE);
}

/* 返回网页文件 */
void serve_file(struct evhttp_request *req, const char *path)
{
    struct stat st, st_p;      // 获取文件
    if (stat(path, &st) == -1) // 请求文件不存在
    {
        printf("LINE %d: %s%s\n", __LINE__, path, "-file not found");
        evhttp_send_error(req, HTTP_NOTFOUND, NULL); // 文件未找到
        return;
    }
    else
    {
        if (S_ISDIR(st.st_mode)) // 若path是目录，自动添加index.html
        {
            strcat(path, "/index.html");
            if (stat(path, &st_p) == -1)
            {
                printf("LINE %d: %s%s\n", __LINE__, path, "-file not found");
                evhttp_send_error(req, HTTP_NOTFOUND, NULL); // 文件未找到
                return;
            }
            st = st_p;
        }

        int fd = -1;
        char *type = get_content_type(path);    // 获取文件类型
        if ((fd = open(path, O_RDONLY)) < 0 || fstat(fd, &st) < 0)
        {
            printf("LINE %d: %s\n", __LINE__, "Open | fstat failed.");
            evhttp_send_error(req, HTTP_NOTFOUND, NULL); // 文档打开失败
            close(fd);
            return;
        }
        // 添加响应头信息
        evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", type);
        struct evbuffer *buf = NULL;    // 初始化返回客户端的数据缓存
        ev_off_t offset = 0;
        size_t bytes_left = 0, bytes_to_read = 0;
        evhttp_send_reply_start(req, HTTP_OK, "OK");    // 分块传输
        while (offset < st.st_size)
        {
            buf = evbuffer_new();
            bytes_left = st.st_size - offset;
            bytes_to_read = bytes_left > MAX_BUF_SIZE ? MAX_BUF_SIZE : bytes_left;
            evbuffer_add_file(buf, fd, offset, bytes_to_read);
            evhttp_send_reply_chunk(req, buf);
            offset += bytes_to_read;
            evbuffer_free(buf);
        }
        evhttp_send_reply_end(req); // 结束分块
        close(fd);
    }
}

/* 处理GET请求 */
void handle_get_request(struct evhttp_request *req, void *arg)
{
    // 解析URI参数
    char *decode_uri = strdup((char *)evhttp_request_uri(req)); // get url
    struct evkeyvalq http_query;                                // get argument
    // 请求中包含 ..
    if (strstr(decode_uri, ".."))
    {
        printf("LINE %d: %s\n", __LINE__, "Get a request include '..'.");
        evhttp_send_error(req, HTTP_BADREQUEST, "Are You Hacking Me?");
        free(decode_uri);
        return;
    }
    // 参数错误
    if (evhttp_parse_query(decode_uri, &http_query) == -1)
    {
        printf("LINE %d: %s\n", __LINE__, "Get request parm failed");
        free(decode_uri);
        evhttp_send_error(req, HTTP_BADREQUEST, NULL);
        return;
    }

    char path[512];
    sprintf(path, "%s%s", WEB_PATH, strtok(decode_uri, "?"));   // 网页文件路径
    if (path[strlen(path) - 1] == '/') // 默认找路径下的index.html
        strcat(path, "index.html");
    
    // 遍历输出参数
    struct evkeyval *header;
	for (header = http_query.tqh_first; header; header = header->next.tqe_next) 
    {
		printf("  %s: %s\n", header->key, header->value);
	}

    serve_file(req, path);  // 向客户端返回数据
    free(decode_uri);
}

/* 处理POST请求 */
void handle_post_request(struct evhttp_request *req, void *arg)
{
    // 处理post请求数据
    size_t post_size = evbuffer_get_length(req->input_buffer); //获取数据长度
	if (post_size <= 0)
	{
        printf("LINE %d: %s\n", __LINE__, "Post message is empty");
        evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request: Message is empty!");
		return;
	}
    size_t copy_len = post_size > MAX_BUF_SIZE ? MAX_BUF_SIZE : post_size;
    char buf[MAX_BUF_SIZE] = {0};
    printf("LINE %d: Post len:%d, Copy len:%d\n", __LINE__, post_size, copy_len);
    memcpy(buf, evbuffer_pullup(req->input_buffer, -1), copy_len);
    buf[post_size] = '\0';
    // printf("LINE %d: Post message:%s\n", __LINE__, buf);
    if(buf == NULL)
	{
		printf("LINE %d: %s\n", __LINE__, "Get a null msg.");
        evhttp_send_error(req, HTTP_BADREQUEST, NULL);
		return;
	}
    else
	{
		// 可以使用json库解析需要的数据
        printf("LINE %d: Request data:%s\n", __LINE__, buf);
	}

    // 初始化返回客户端的数据缓存
    struct evbuffer *buf_ret = evbuffer_new();
    if (buf_ret == NULL)
    {
        printf("LINE %d: %s\n", __LINE__, "Reply buf is null.");
        evhttp_send_error(req, HTTP_BADREQUEST, NULL);
        return;
    }
    // 分块传输
    evhttp_send_reply_start(req, HTTP_OK, "Client");
    evbuffer_add_printf(buf_ret, "This is chunk1!");
    evhttp_send_reply_chunk(req, buf_ret);
    evbuffer_add_printf(buf_ret, "This is chunk2!");
    evhttp_send_reply_chunk(req, buf_ret);
    evhttp_send_reply_end(req);
    evbuffer_free(buf_ret);
}

void handle_head_request(struct evhttp_request *req, void *arg)
{
    evhttp_send_error(req, HTTP_NOTIMPLEMENTED, NULL);
}

void handle_put_request(struct evhttp_request *req, void *arg)
{
    evhttp_send_error(req, HTTP_NOTIMPLEMENTED, NULL);
}

void handle_delete_request(struct evhttp_request *req, void *arg)
{
    evhttp_send_error(req, HTTP_NOTIMPLEMENTED, NULL);
}

void handle_options_request(struct evhttp_request *req, void *arg)
{
    evhttp_send_error(req, HTTP_NOTIMPLEMENTED, NULL);
}

void handle_trace_request(struct evhttp_request *req, void *arg)
{
    evhttp_send_error(req, HTTP_NOTIMPLEMENTED, NULL);
}

void handle_connect_request(struct evhttp_request *req, void *arg)
{
    evhttp_send_error(req, HTTP_NOTIMPLEMENTED, NULL);
}

void handle_patch_request(struct evhttp_request *req, void *arg)
{
    evhttp_send_error(req, HTTP_NOTIMPLEMENTED, NULL);
}

void handle_unknown_request(struct evhttp_request *req, void *arg)
{
    evhttp_send_error(req, HTTP_NOTIMPLEMENTED, NULL);
}

/* 处理客户端请求 */
void accept_request(struct evhttp_request *req, void *arg)
{
    if (req == NULL)
    {
        printf("LINE %d: %s\n", __LINE__, "Get a null request");
        evhttp_send_error(req, HTTP_BADREQUEST, NULL);
        return;
    }
    switch (evhttp_request_get_command(req))    // 请求类型
    {
    case EVHTTP_REQ_GET:
        handle_get_request(req, arg);
        break;
    case EVHTTP_REQ_POST:
        handle_post_request(req, arg);
        break;
    case EVHTTP_REQ_HEAD:
        handle_head_request(req, arg);
        break;
    case EVHTTP_REQ_PUT:
        handle_put_request(req, arg);
        break;
    case EVHTTP_REQ_DELETE:
        handle_delete_request(req, arg);
        break;
    case EVHTTP_REQ_OPTIONS:
        handle_options_request(req, arg);
        break;
    case EVHTTP_REQ_TRACE:
        handle_trace_request(req, arg);
        break;
    case EVHTTP_REQ_CONNECT:
        handle_connect_request(req, arg);
        break;
    case EVHTTP_REQ_PATCH:
        handle_patch_request(req, arg);
        break;
    default:
        handle_unknown_request(req, arg);
        break;
    }
}

/* 获取文件类型 */
char *get_content_type(char *path)
{
    char *ext, *ret = strrchr(path, '.');
    struct table_entry *ent;
    if (!ret || strchr(ret, '/'))
        return "application/misc";
    ext = ret + 1;
    for (ent = &content_type_table[0]; ent->extension; ++ent)
    {
        if (!evutil_ascii_strcasecmp(ent->extension, ext))
            return ent->content_type;
    }
}

/* 启动HTTP线程 */
void http_startup(void)
{
    struct event_base *evbase = event_init();   // 初始化evbase
    if(evbase == NULL)
    {
        printf("LINE %d: %s\n", __LINE__, "HTTP evbase create failed");
        return;
    }

    char *http_addr = "0.0.0.0";
    struct evhttp *http_server = evhttp_start(http_addr, HTTP_SERVER_PORT);    // 启动http服务端
    if (http_server == NULL)
    {
        printf("LINE %d: %s\n", __LINE__, "HTTP evhttp create failed");
        return;
    }
    evhttp_set_gencb(http_server, accept_request, NULL);   // 设置事件处理函数
    event_dispatch();   // 循环监听
    evhttp_free(http_server);
    return;
}

/* 启动HTTPS线程 */
void https_startup(void)
{
    SSL_CTX *ctx = evssl_init(); // 初始化ssl
    if (ctx == NULL)
    {
        printf("LINE %d: %s\n", __LINE__, "SSL init failed");
        return;
    }
    /*
    * event_base_new()
    * event_init() 是否多线程共享
    */
    struct event_base *evbase = event_init();   // 创建HTTPS线程的event_base
    if(evbase == NULL)
    {
        printf("LINE %d: %s\n", __LINE__, "HTTPS evbase create failed");
        return;
    }

    char *https_addr = "0.0.0.0";
    struct evhttp *https_server = evhttp_start(https_addr, HTTPS_SERVER_PORT);   // 创建evhttp以处理请求  
    if(https_server == NULL)
    {
        printf("LINE %d: %s\n", __LINE__, "HTTPS evhttp create failed");
        return;
    }
    evhttp_set_bevcb (https_server, bevcb, ctx);    // magic

    evhttp_set_gencb(https_server, accept_request, NULL);  // 设置事件处理函数
    event_dispatch();   // 循环监听
    evhttp_free(https_server);
    SSL_CTX_free(ctx);
    return;
}

int main()
{
    pthread_t thread_http, thread_https;
    // 创建http线程和https线程
    if (pthread_create(&thread_http, NULL, http_startup, NULL) != 0)
    {
        printf("LINE %d: %s\n", __LINE__, "HTTP pthread_create failed");
        return;
    }
    if (pthread_create(&thread_https, NULL, https_startup, NULL) != 0)
    {
        printf("LINE %d: %s\n", __LINE__, "HTTPS pthread_create failed");
        return;
    }
    while (1)
    {
    }
    return 0;
}