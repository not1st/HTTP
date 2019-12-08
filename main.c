#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>

#define CLIENT_LIMIT 10
#define WEB_PATH "www"

int startup(int *);
void accept_request(int);
int get_line(int, char *, int);
void unimplemented(int);
void not_found(int);
void error_die(const char *);
void serve_file(int, const char *);
void cannot_execute(int);
void execute_cgi(int, const char *, const char *, const char *);

/* 程序异常终止 */
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/* 初始化服务器配置并启动监听 */
int startup(int *port)
{
    int httpd = 0, socket_len;
    struct sockaddr_in server_addr;

    // 初始化服务器信息
    socket_len = sizeof(struct sockaddr_in);
    memset(&server_addr, 0, socket_len);
    server_addr.sin_family = AF_INET;         // 地址族
    server_addr.sin_port = htons(*port);      // 端口号
    server_addr.sin_addr.s_addr = INADDR_ANY; // IP地址
    // bzero(&(server_addr.sin_zero), 8);

    // 创建socket
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket create failed");
    // 绑定服务器信息
    if (bind(httpd, (struct sockaddr *)&server_addr, socket_len) < 0)
        error_die("bind failed");
    // 动态端口
    if (*port == 0)
    {
        if (getsockname(httpd, (struct sockaddr *)&server_addr, &socket_len) == -1)
            error_die("getsockname failed");
        *port = ntohs(server_addr.sin_port);
    }
    // 开始监听
    if (listen(httpd, CLIENT_LIMIT) < 0)
        error_die("listen failed");
    return (httpd);
}

/* 处理HTTP请求 */
void accept_request(int client)
{
    char buf[1024];
    int cgi = 0, len, i, j;
    char method[255];
    char url[255];
    char path[512];
    char version[16];
    struct stat st;
    char *query_string = NULL;

    // 获取请求行信息（请求方法、URL、HTTP版本）
    len = get_line(client, buf, sizeof(buf));

    // 获取请求方法（GET、POST、...）
    for (i = 0; !isspace((int)(buf[i])) && (i < sizeof(method) - 1); i++)
    {
        method[i] = buf[i];
    }
    method[i] = '\0';

    // 非GET、POST方法
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return;
    }

    // 获取URL
    for (i++, j = 0; !isspace((int)(buf[i])) && (j < sizeof(url) - 1); i++, j++)
    {
        url[j] = buf[i];
    }
    url[i] = '\0';

    // 获取HTTP版本
    for (i++, j = 0; !isspace((int)(buf[i])) && (j < sizeof(version) - 1) && i < len; i++, j++)
    {
        version[j] = buf[i];
    }
    version[i] = '\0';

    // POST请求：开启CGI
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    // 处理GET请求
    if (strcasecmp(method, "GET") == 0)
    {
        // 获取请求参数?key=value
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        // 有参数
        if (*query_string == '?')
        {
            cgi = 1;
            // 截断并切割得到参数部分字符串
            *query_string = '\0';
            query_string++;
        }
    }
    // 拼接网页文件路径
    sprintf(path, "%s%s", WEB_PATH, url);
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    // 获取文件
    if (stat(path, &st) == -1)
    {
        // 文件未找到，丢弃所有头信息
        while ((len > 0) && strcmp("\n", buf))
            len = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
        if (S_ISDIR(st.st_mode))
            strcat(path, "/index.html");
        // 文件所有者、用户组、其他用户有可执行权限
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
            cgi = 1;
        // 不需要CGI程序处理
        if (!cgi)
            serve_file(client, path);
        else
            execute_cgi(client, path, method, query_string);
    }

    close(client);
}

/* 处理动态请求 */
void execute_cgi(int client, const char *path, const char *method, const char *query_string)
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A';
    buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)
        while ((numchars > 0) && strcmp("\n", buf))
            numchars = get_line(client, buf, sizeof(buf));
    else // POST
    {
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            // 获取 HTTP 消息传输长度
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1)
        {
            bad_request(client);
            return;
        }
    }
    
    // 响应头信息
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    // 建立两条管道，用来读、写，并创建一个子进程
    if (pipe(cgi_output) < 0 || pipe(cgi_input) < 0 || (pid = fork()) < 0)
    {
        cannot_execute(client);
        return;
    }
    // 0：子进程；+x：父进程
    if (pid == 0)
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], 1);
        dup2(cgi_input[0], 0);
        close(cgi_output[0]);
        close(cgi_input[1]);
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0)
        {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else
        { /* POST */
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        execl(path, path, NULL);
        exit(0);
    }
    else
    { /* parent */
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
            for (i = 0; i < content_length; i++)
            {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);
    }
}

/* 接收一行请求信息 */
int get_line(int sock, char *buf, int size)
{
    int i = 0, n;
    char c = '\0';

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    return i;
}

/* 响应网页文件 */
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];
    char headers[] = "HTTP/1.0 200 OK\r\n"
                     "Content-Type: text/html\r\n\r\n";

    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        // 返回头信息
        send(client, headers, sizeof(headers), 0);
        // 返回网页文件
        fgets(buf, sizeof(buf), resource);
        while (!feof(resource))
        {
            send(client, buf, strlen(buf), 0);
            fgets(buf, sizeof(buf), resource);
        }
    }
    fclose(resource);
}

/* 501：未实现的功能 */
void unimplemented(int client)
{
    char buf[] = "HTTP/1.0 501 Method Not Implemented\r\n"
                 "Content-Type: text/html\r\n\r\n"
                 "Method Not Implemented";
    send(client, buf, strlen(buf), 0);
}

/* 404：文件未找到 */
void not_found(int client)
{
    char buf[] = "HTTP/1.0 404 NOT FOUND\r\n"
                 "Content-Type: text/html\r\n\r\n"
                 "The resource specified is unavailable.\r\n";
    send(client, buf, strlen(buf), 0);
}

/* 500：服务器执行错误 */
void cannot_execute(int client)
{
    char buf[] = "HTTP/1.0 500 Internal Server Error\r\n"
                 "Content-type: text/html\r\n\r\n"
                 "Error prohibited CGI execution.\r\n";
    send(client, buf, strlen(buf), 0);
}

/* 400：坏请求 */
void bad_request(int client)
{
    char buf[] = "HTTP/1.0 400 BAD REQUEST\r\n"
                 "Content-type: text/html\r\n\r\n"
                 "Your browser sent a bad request";
    send(client, buf, strlen(buf), 0);
}

int main(int argc, char *argv[])
{
    int server_fd = -1, client_fd = -1;
    int port = 80;
    struct sockaddr_in client_addr;
    int socket_len = sizeof(client_addr);
    pthread_t new_thread;

    // 初始化服务器并开始监听
    server_fd = startup(&port);
    printf("httpd running on port %d\n", port);

    // 处理客户端连接
    while (1)
    {
        // 收到客户端连接
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &socket_len);
        if (client_fd == -1)
            error_die("accept error");
        // 创建新线程
        if (pthread_create(&new_thread, NULL, accept_request, client_fd) != 0)
            perror("pthread_create failed");
    }

    colse(server_fd);
    return 0;
}