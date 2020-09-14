# 基于 libevent 的 HTTPS 服务器



## 一、系统介绍

1. 支持 HTTP Post/Get 请求方法
2. 支持上传/下载文件
3. 支持 HTTP 分块传输
4. 支持 HTTP 持久连接
5. 同时支持 HTTP & HTTPS 服务
6. 支持多路并发
7. 支持 CGI 程序执行
8. 支持常见 Web URI 攻击防御策略



## 二、系统开发环境搭建

### 2.1 开发环境

1. 操作系统：WSL Ubuntu 18.04
2. 开发工具：VS Code 1.40.2
3. 编译器：gcc 7.4.0

### 2.2 环境搭建

1. 安装 gcc 和 gdb

   ```shell
   $ sudo apt update
   $ sudo apt upgrade
   $ sudo apt install build-essential
   $ sudo apt install gdb
   // 验证安装
   $ whereis g++
   $ whereis gcc
   $ whereis gdb
   ```

2. 安装 OpenSSL

   ```shell
   $ sudo apt install openssl
   $ sudo apt install libssl-dev
   
   // 验证安装
   $ openssl version -a
   ```

3. 安装 libevent

   ```shell
   // 官网下载 libevent：http://libevent.org/
   
   $ tar -zxvf libevent-2.1.11-stable.tar.gz
   $ cd libevent-2.1.11-stable/
   $ ./configure --prefix=/usr	// 记住此路径，编译时会用到
   $ make
   $ sudo make install
   
   // 验证安装
   $ ls -al /usr/lib | grep libevent
   ```

   如果出现“aclocal-1.16: 未找到命令”错误，执行命令 `autoreconf -ivf` 后继续安装 或 安装旧版本，再覆盖安装新版本。

4. 生成自签名证书

   ```
   文档：使用OpenSSL生成自签名证书.md
   链接：http://note.youdao.com/noteshare?id=c07938df9289fa99308e16f4693bf9e6&sub=WEBc18238b7193dc74959990f6b629077b1
   ```

5. 安装 cJSON

   ```shell
   // 从 git 或官网下载 cjson
   $ git clone https://github.com/arnoldlu/cJSON.git
   
   $ cd cJSON-master/
   $ sudo make
   $ sudo make install
   $ ldconfig
   
   /*
   cjson 的库文件会默认安装在/usr/local/lib 下，这会导致 gcc 在链接时找不到库文件，如出现错误：error while loading shared libraries: libcjson.so: cannot open shared object file: No such file or directory
   以下为解决办法：
   */
   $ sudo vim /etc/ld.so.conf	// 在最后一行添加：/usr/local/lib
   $ sudo ldconfig	// 刷新缓存
   
   // 验证
   $ ldconfig -p
   ```

6. 编译参数设置

   ```shell
   // libevent：此处的路径即安装 libevent 时的路径
   -I /usr/include/ -L /usr/lib/ -levent
   
   // 多线程
   -lpthread
   
   // cJSON
   -lcjson -lm
   
   // OpenSSL(levent不需要重复设置，可省略)
   -lssl -lcrypto -levent -levent_openssl
   
   // finally
   $ /usr/bin/gcc -g server.c -I /usr/include/ -L /usr/lib/ -lssl -lcrypto -levent -levent_openssl -lpthread -lcjson -lm -o /home/yc/http/server
   ```

   在 VS Code 中设置 tasks.json 即可。
   ![设置 GCC 编译选项](https://github.com/not1st/HTTP/blob/master/images/image-20191211000817715.png)



### 2.3 程序结构

|--- **.vscode** [vs code项目配置文件]

|--- **discard** [包含开发过程中另外三个版本不同实现的 http server 源码]

|--- **doc** [下载文件的默认路径，内含一个 test 文档]

|--- **images** [提供本文档所用到的图像，与项目无关]

|--- **upload** [默认上传至此路径]

|--- **www** [默认存放网页文件]

|--- **server.c** [程序源码]

|--- **server.crt** [生成自签名证书的一部分]

|--- **server.key** [生成自签名证书的一部分]

### 2.4 程序运行

1. 启动 server.exe 或 编译链接 server.c
2. 使用浏览器访问http://server_ip:8000 或 https://server_ip:4430



## 三、系统功能



### 3.1 HTTP Post / Get 方法

&emsp;&emsp;HTTP定义了客户端与服务器交互的不同方法，最基本的方法即为Post/Get方法，分别用于更新、查询服务器资源。本节将说明本项目何时调用Get/Post请求。

&emsp;&emsp;（1）运行server.exe启动服务器，客户端浏览器访问http://server_ip:8000，即可访问本服务器主页；

&emsp;&emsp;（2）打开浏览器调试工具，即可看到客户端访问服务器使用的请求方法，下图展示了服务器支持的两种请求：

 

![服务器支持的get & post请求](https://github.com/not1st/HTTP/blob/master/images/clip_image001.jpg)

### 3.2 上传 & 下载文件

&emsp;&emsp;文件上传&下载为服务器基础功能之一，我们在实现Post/Get方法的基础上实现了单个文件的上传&下载。系统提供了客户端测试页面进行各种模块的测试，在该页面可选择系统文件进行上传，下图展示了从系统选择test.txt文件并上传到upload目录下的过程：


 ![上传功能展示](https://github.com/not1st/HTTP/blob/master/images/clip_image002.jpg)

 

&emsp;&emsp;在主页可选择点击下载服务器端资源文件，文件服务器默认为服务器文件夹的doc目录下。

 

![下载功能展示](https://github.com/not1st/HTTP/blob/master/images/clip_image003.jpg)

### 3.3 HTTP 分块传输

&emsp;&emsp;分块传输编码（Chunked transfer encoding）是HTTP中的一种数据传输机制，允许HTTP由应用服务器发送给客户端应用（通常是网页浏览器）的数据可以分成多个部分。我们使用分块传输并规定每块大小buf_size可以有效提升资源传输效率并及时告知客户端应答消息的结束时间。实现分块传输的验证方法下文所示。

&emsp;&emsp;访问服务器端页面时使用Wireshark软件进行抓包，过滤得到访问主页的HTTP流量包，可在其中找到分块传输chunk字段。本例中块大小buf_size为1024，可在sever.c中访问修改本参数，如下图：

![HTTP Chunk技术](https://github.com/not1st/HTTP/blob/master/images/clip_image004.jpg)


### 3.4 HTTP 持久连接

&emsp;&emsp;HTTP连接有两种连接方式：短连接和长连接；短连接即一次请求对应一次TCP连接的建立和销毁过程，而长连接是多个请求共用同一个连接这样可以节省大量连接建立时间提高通信效率。目前主流浏览器都会在请求头里面包含Connection:keep-alive字段，该字段的作用就是告诉HTTP服务器响应结束后不要关闭连接，浏览器会将建立的连接缓存起来，当在有限时效内有再次对相同服务器发送请求时则直接从缓存中取出连接进行通信。当然被缓存的连接如果空闲时间超过了设定值则会关闭连接。

&emsp;&emsp;在Wireshark软件抓包即可看到Connection:keep-alive字段：

![HTTP持久连接](https://github.com/not1st/HTTP/blob/master/images/clip_image005.jpg)

### 3.5 同时支持 HTTP & HTTPS 服务

&emsp;&emsp;HTTPS （Hyper Text Transfer Protocol over SecureSocket Layer）,是以安全为目标的 HTTP 通道，在HTTP的基础上通过传输加密和身份认证保证了传输过程的安全性。HTTPS安全的基础是SSL，现在被广泛应用于万维网上的敏感数据通讯，如交易支付等方面。

&emsp;&emsp;本服务器并行双线程同时运行HTTP与HTTPS服务。我们在不断开HTTP连接的情况下，可通过https://server_ip:4430访问本服务器主页，且可在HTTPS协议下实现所有功能。

### 3.6 基于 libevent 的多路并发



### 3.7 支持 CGI 程序执行

&emsp;&emsp;CGI（Common Gateway Interface）是指Web服务器在接收到客户端发送过来的请求后转发给程序的一组机制。在CGI的作用下，程序会对请求内容做出响应的动作，比如处理用户输入、创建HTML等动态内容，具体流程如下图所示：

![CGI原理图](https://github.com/not1st/HTTP/blob/master/images/clip_image006.jpg)


&emsp;&emsp;本服务器后端支持CGI程序调用执行，通过execute_cgi函数识别客户端传递的参数并调用对应CGI程序后返回运行结果。本例中仅实现因数分解CGI程序作为测试：

![因数分解CGI执行演示](https://github.com/not1st/HTTP/blob/master/images/clip_image007.jpg)



### 3.8 常见 Web URI 攻击防御策略

&emsp;&emsp;许多的Web应用程序一般会有对服务器的文件读取查看的功能，大多会用到提交的参数来指明文件名。但是由于文件名可以任意更改而服务器支持 “~/”，“../” 等特殊符号的目录回溯，从而使攻击者越权访问或者覆盖敏感数据，如网站的配置文件、系统的核心文件，这样的缺陷被命名为路径遍历漏洞。

&emsp;&emsp;本服务器可防御常见的目录遍历攻击，服务器可自动过滤如访问上层目录地址的”..”字段，并返回提示“Are you hacking me?”。



## 四、系统测试

&emsp;&emsp;软件压力测试是一种基本的质量保证行为，它是每个重要软件测试工作的一部分。软件压力测试的基本思路很简单：不是在常规条件下运行手动或自动测试，而是在计算机数量较少或系统资源匮乏的条件下运行测试。我们通过JMeter和PostMan软件进行软件压力测试来观察服务器响应时间、并发数用户数、吞吐量、资源利用率等的性能指标。



### 4.1 并发测试

&emsp;&emsp;我们模拟了500个线程进行测试页面的访问，结果显示，全部页面请求都可以正常返回响应，其响应时间如图4-1。最小响应时间：3ms，最大响应时间：98ms，平均响应时间：9ms。

 

![并发执行500个线程](https://github.com/not1st/HTTP/blob/master/images/clip_image008.jpg)



&emsp;&emsp;再次模拟2000个线程进行测试页面的访问，全部页面请求都可以正常返回响应，其响应时间如下图。可能受限于笔记本电脑的性能，响应时间明显变长：

![并发执行2000个线程](https://github.com/not1st/HTTP/blob/master/images/clip_image009.jpg)


### 4.2 上传文件测试

&emsp;&emsp;我们模拟了200个线程，每个线程上传5次文件。结果显示成功率达到了100%，其响应时间如图4-3，平均时延630ms。

![上传文件测试](https://github.com/not1st/HTTP/blob/master/images/clip_image0010.jpg)
