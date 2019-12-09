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
void https_accept_request(struct evconnlistener *, int, struct sockaddr *, int, void *);
void handle_https_request(struct bufferevent *, void *);
void handle_https_response(struct bufferevent *, void *);
void http_accept_request(struct evhttp_request *, void *);
char *get_content_type(char *);
void http_startup(void);
void https_startup(void);
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

void handle_https_request(struct bufferevent *bev, void *arg)
{
    struct evbuffer *in = bufferevent_get_input(bev);

    printf("DEBUG:Received %zu bytes\n", evbuffer_get_length(in));
    printf("------ data -----\n");
    printf("%.*s\n", (int)evbuffer_get_length(in), evbuffer_pullup(in, -1));

    bufferevent_write_buffer(bev, in);
}

void handle_https_response(struct bufferevent *bev, void *arg)
{

}

/*  */
void https_accept_request(struct evconnlistener *serv, int sock, struct sockaddr *sa, int sa_len, void *arg)
{
    struct event_base *evbase;
    struct bufferevent *bev;
    SSL_CTX *server_ctx;
    SSL *client_ctx;
    server_ctx = (SSL_CTX *)arg;
    client_ctx = SSL_new(server_ctx);
    evbase = evconnlistener_get_base(serv);
    bev = bufferevent_openssl_socket_new(evbase, sock, client_ctx, BUFFEREVENT_SSL_ACCEPTING, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_enable(bev, EV_READ);
    bufferevent_setcb(bev, handle_https_request, handle_https_response, NULL, NULL);
}

/* 处理GET请求 */
void handle_get_request(struct evhttp_request *req, void *arg)
{
    if (req == NULL)
    {
        puts("get a null 'get' request");
        evhttp_send_error(req, HTTP_BADREQUEST, NULL);
        return;
    }
    // 解析URI参数
    char *decode_uri = strdup((char *)evhttp_request_uri(req)); // get uri
    struct evkeyvalq http_query;                                // get argument
    struct evkeyval *header;
    // 请求中包含 ..
    if (strstr(decode_uri, ".."))
    {
        evhttp_send_error(req, HTTP_BADREQUEST, NULL);
        free(decode_uri);
        return;
    }
    // 参数错误
    if (evhttp_parse_query(decode_uri, &http_query) == -1)
    {
        puts("evhttp_parse_query failed");
        free(decode_uri);
        evhttp_send_error(req, HTTP_BADREQUEST, NULL);
        return;
    }

    char path[512]; // 网页文件路径
    sprintf(path, "%s%s", WEB_PATH, strtok(decode_uri, "?"));
    if (path[strlen(path) - 1] == '/') // 默认找路径下的index.html
        strcat(path, "index.html");
    puts(path);
    // 遍历输出参数
	for (header = http_query.tqh_first; header; header = header->next.tqe_next) 
    {
		printf("  %s: %s\n", header->key, header->value);
	}

    struct stat st, st_p;      // 获取文件
    int cgi = 0, fd = -1;      // 处理cgi程序
    if (stat(path, &st) == -1) // 请求文件不存在
    {
        evhttp_send_error(req, HTTP_BADREQUEST, "Not Found"); // 文件未找到
        free(decode_uri);
        return;
    }
    else
    {
        if (S_ISDIR(st.st_mode)) // 若rul是目录，自动添加index.html
        {
            strcat(path, "/index.html");
            if (stat(path, &st_p) == -1)
            {
                evhttp_send_error(req, HTTP_NOTFOUND, NULL); // 文件未找到
                free(decode_uri);
                return;
            }
            st = st_p;
        }
        // 文件所有者、用户组、其他用户有可执行权限
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
            cgi = 0;
        if (!cgi) // 不需要CGI程序处理
        {
            char *type = get_content_type(path);
            if ((fd = open(path, O_RDONLY)) < 0 || fstat(fd, &st) < 0)
            {
                perror("open | fstat");
                evhttp_send_error(req, HTTP_NOTFOUND, NULL); // 文档打开失败
                close(fd);
                free(decode_uri);
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
        else // cgi
            evhttp_send_error(req, HTTP_NOTIMPLEMENTED, NULL);
    }
    free(decode_uri);
}

/* 处理POST请求 */
void handle_post_request(struct evhttp_request *req, void *arg)
{
    if (req == NULL)
    {
        printf("LINE %d: %s\n", __LINE__, "Get a null request");
        evhttp_send_error(req, HTTP_BADREQUEST, NULL);
        return;
    }
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
void handle_head_request(struct evhttp_request *req, void *arg) {}
void handle_put_request(struct evhttp_request *req, void *arg) {}
void handle_delete_request(struct evhttp_request *req, void *arg) {}
void handle_options_request(struct evhttp_request *req, void *arg) {}
void handle_trace_request(struct evhttp_request *req, void *arg) {}
void handle_connect_request(struct evhttp_request *req, void *arg) {}
void handle_patch_request(struct evhttp_request *req, void *arg) {}
void handle_unknown_request(struct evhttp_request *req, void *arg) {}

/* 处理HTTP请求 */
void http_accept_request(struct evhttp_request *req, void *arg)
{
    // HTTP请求类型
    switch (evhttp_request_get_command(req))
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
    struct evhttp *http_server = NULL;
    char *http_addr = "0.0.0.0";

    // 初始化
    event_init();
    // 启动http服务端
    http_server = evhttp_start(http_addr, HTTP_SERVER_PORT);
    if (http_server == NULL)
    {
        perror("http server start failed.");
        return;
    }
    evhttp_set_gencb(http_server, http_accept_request, NULL); // 设置事件处理函数
    event_dispatch();                                         // 循环监听
    evhttp_free(http_server);                                 // 实际上不会释放，代码不会运行到这一步
    return;
}

/* 启动HTTPS线程 */
void https_startup(void)
{
    SSL_CTX *ctx;
    struct evconnlistener *listener;
    struct event_base *evbase;
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(HTTPS_SERVER_PORT);
    sin.sin_addr.s_addr = INADDR_ANY;
    ctx = evssl_init(); // 初始化ssl
    if (ctx == NULL)
        return;
    evbase = event_base_new(); // 创建HTTPS线程的event_base
    listener = evconnlistener_new_bind(evbase, https_accept_request, (void *)ctx,
                                       LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
                                       1024, (struct sockaddr *)&sin, sizeof(sin));
    event_base_loop(evbase, 0);
    evconnlistener_free(listener);
    SSL_CTX_free(ctx);
    return;
}

//解析http头，主要用于get请求时解析uri和请求参数
char *find_http_header(struct evhttp_request *req, struct evkeyvalq *params, const char *query_char)
{
    if (req == NULL || params == NULL || query_char == NULL)
    {
        printf("====line:%d,%s\n", __LINE__, "input params is null.");
        return NULL;
    }

    struct evhttp_uri *decoded = NULL;
    char *query = NULL;
    char *query_result = NULL;
    const char *path;
    const char *uri = evhttp_request_get_uri(req); //获取请求uri

    if (uri == NULL)
    {
        printf("====line:%d,evhttp_request_get_uri return null\n", __LINE__);
        return NULL;
    }
    else
    {
        printf("====line:%d,Got a GET request for <%s>\n", __LINE__, uri);
    }

    //解码uri
    decoded = evhttp_uri_parse(uri);
    if (!decoded)
    {
        printf("====line:%d,It's not a good URI. Sending BADREQUEST\n", __LINE__);
        evhttp_send_error(req, HTTP_BADREQUEST, 0);
        return NULL;
    }

    //获取uri中的path部分
    path = evhttp_uri_get_path(decoded);
    if (path == NULL)
    {
        path = "/";
    }
    else
    {
        printf("====line:%d,path is:%s\n", __LINE__, path);
    }

    //获取uri中的参数部分
    query = (char *)evhttp_uri_get_query(decoded);
    if (query == NULL)
    {
        printf("====line:%d,evhttp_uri_get_query return null\n", __LINE__);
        return NULL;
    }

    //查询指定参数的值
    evhttp_parse_query_str(query, params);
    query_result = (char *)evhttp_find_header(params, query_char);

    return query_result;
}

int main()
{
    pthread_t thread_http, thread_https;
    // 创建http线程和https线程
    if (pthread_create(&thread_http, NULL, http_startup, NULL) != 0)
        perror("http pthread_create failed");
    if (pthread_create(&thread_https, NULL, https_startup, NULL) != 0)
        perror("https pthread_create failed");
    // 可加菜单
    while (1)
    {
    }
    return 0;
}