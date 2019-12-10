# 基于 livevent 的 HTTPS 服务器



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
2. 使用浏览器访问http://your_ip:8000 或 https://your_ip:4430



## 三、系统功能



### 3.1 HTTP Post / Get 方法



### 3.2 上传 & 下载文件



### 3.3 HTTP 分块传输



### 3.4 HTTP 持久连接



### 3.5 同时支持 HTTP & HTTPS 服务



### 3.6 基于 libevent 的多路并发



### 3.7 支持 CGI 程序执行



### 3.8 常见 Web URI 攻击防御策略





## 四、系统测试





支持