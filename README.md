# Hello World!

## 一、开发环境

1. OpenSSL安装
```
sudo apt-get install openssl
sudo apt-get install libssl-dev

openssl version -a
```

2. libevent安装
```
./configure --prefix=/usr
make
# 如果出现 aclocal-1.16: 未找到命令
autoreconf  -ivf
sudo make install
```
