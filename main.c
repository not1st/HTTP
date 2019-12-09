#include <stdio.h>
#include <stdlib.h>
#include <evhttp.h>
#include <event.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

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

#define SERVER_CRT "server.crt"
#define SERVER_KEY "server.key"
#define BUF_MAX 1024 * 16
#define HTTP_SERVER_PORT 8000
#define HTTPS_SERVER_PORT 4430

SSL_CTX *evssl_init(void);
void https_accept_request(struct evconnlistener *, int, struct sockaddr *, int, void *);
void ssl_readcb(struct bufferevent *, void *);
void error_die(const char *);
void accept_request(struct evhttp_request *, void *);
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

void ssl_readcb(struct bufferevent *bev, void *arg)
{
    struct evbuffer *in = bufferevent_get_input(bev);

    printf("DEBUG:Received %zu bytes\n", evbuffer_get_length(in));
    printf("------ data -----\n");
    printf("%.*s\n", (int)evbuffer_get_length(in), evbuffer_pullup(in, -1));

    bufferevent_write_buffer(bev, in);
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
    bufferevent_setcb(bev, ssl_readcb, NULL, NULL, NULL);
}

/* 处理GET请求 */
void handle_get_request(struct evhttp_request *req, void *arg)
{
    if (req == NULL)
    {
        printf("get a null 'get' request");
        return;
    }
    // 解析URL参数
    char *decode_uri = strdup((char *)evhttp_request_uri(req));
    struct evkeyvalq http_query;
    if (evhttp_parse_query(decode_uri, &http_query) == -1)
    {
        printf("evhttp_parse_query failed");
        free(decode_uri);
        return;
    }
    free(decode_uri);

    // 初始化返回客户端的数据缓存
    struct evbuffer *buf = evbuffer_new();
    if (buf == NULL)
    {
        printf("reply buf is null.");
        return;
    }
    evbuffer_add_printf(buf, "Receive get request,Thanks for the request!");
    evhttp_send_reply(req, HTTP_OK, "Client", buf);
    evbuffer_free(buf);
}

void handle_post_request(struct evhttp_request *req, void *arg)
{
    if (req == NULL)
    {
        printf("get a null 'post' request");
        return;
    }

    // 初始化返回客户端的数据缓存
    struct evbuffer *buf = evbuffer_new();
    if (buf == NULL)
    {
        printf("reply buf is null.");
        return;
    }
    // 分块传输
    evhttp_send_reply_start(req, HTTP_OK, "Client");
    evbuffer_add_printf(buf, "Receive post1 request,Thanks for the request!");
    evhttp_send_reply_chunk(req, buf);
    evbuffer_add_printf(buf, "Receive post2 request,Thanks for the request!");
    evhttp_send_reply_chunk(req, buf);
    evhttp_send_reply_end(req);
    evbuffer_free(buf);
}
void handle_head_request(struct evhttp_request *req, void *arg) {}
void handle_put_request(struct evhttp_request *req, void *arg) {}
void handle_delete_request(struct evhttp_request *req, void *arg) {}
void handle_options_request(struct evhttp_request *req, void *arg) {}
void handle_trace_request(struct evhttp_request *req, void *arg) {}
void handle_connect_request(struct evhttp_request *req, void *arg) {}
void handle_patch_request(struct evhttp_request *req, void *arg) {}
void handle_unknown_request(struct evhttp_request *req, void *arg) {}

/* 程序异常终止 */
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/* 处理HTTP请求 */
void http_accept_request(struct evhttp_request *req, void * arg)
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
        error_die("http server start failed.");
    evhttp_set_gencb(http_server, http_accept_request, NULL);   // 设置事件处理函数
    event_dispatch();   // 循环监听
    evhttp_free(http_server);   // 实际上不会释放，代码不会运行到这一步
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
    evbase = event_base_new();  // 创建HTTPS线程的event_base
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
    while(1){}
    return 0;
}