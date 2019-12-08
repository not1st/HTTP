#include <stdio.h>
#include <stdlib.h>
#include <evhttp.h>
#include <event.h>
#include <string.h>
#include "event2/http.h"
#include "event2/event.h"
#include "event2/buffer.h"
#include "event2/bufferevent.h"
#include "event2/bufferevent_compat.h"
#include "event2/http_struct.h"
#include "event2/http_compat.h"
#include "event2/util.h"
#include "event2/listener.h"

#define BUF_MAX 1024 * 16

void error_die(const char *);
void accept_request(struct evhttp_request *, void *);
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

/* 处理GET请求 */
void handle_get_request(struct evhttp_request *req, void *arg)
{
    if (req == NULL)
    {
        printf("get a null request");
        return;
    }
    /* 分析URL参数 */
    char *decode_uri = strdup((char*) evhttp_request_uri(req)); 
    struct evkeyvalq http_query; 
    // 解析错误
    if(evhttp_parse_query(decode_uri, &http_query) == -1)
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

/* 程序异常终止 */
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/* 处理HTTP请求 */
void accept_request(struct evhttp_request *req, void *arg)
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
    struct evhttp *http_server = NULL;
    short http_port = 8000;
    char *http_addr = "0.0.0.0";

    // 初始化
    event_init();
    // 启动http服务端
    http_server = evhttp_start(http_addr, http_port);
    if (http_server == NULL)
        error_die("http server start failed.");

    // 设置请求超时时间(s)
    evhttp_set_timeout(http_server, 5);
    // 设置事件处理函数，evhttp_set_cb针对每一个事件(请求)注册一个处理函数，
    // evhttp_set_cb(http_server, "/testpost", http_handler_testpost_msg, NULL);
    // evhttp_set_gencb函数，是对所有请求设置一个统一的处理函数
    evhttp_set_gencb(http_server, accept_request, NULL);

    //循环监听
    event_dispatch();

    //实际上不会释放，代码不会运行到这一步
    evhttp_free(http_server);

    return 0;
}