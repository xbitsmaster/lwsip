
## lwsip
lwsip = light weight sip stack for RTOS
用于嵌入式系统的SIP客户端开源软件
基于开源项目开发：
  - [media-server](https://github.com/ireader/media-server) - SIP/RTP protocol implementation
  - [sdk](https://github.com/ireader/sdk) - Basic SDK tools, libice implementation
  - [3rd](https://github.com/ireader/3rd) - Other third-party dependencies
  - [avcodec](https://github.com/ireader/avcodec) - Audio/video codecs, libavcodec implementation
  - [lwip](https://github.com/lwip-tcpip/lwip) - TCP/IP protocol stack (optional), for embedded system
  - [mbedtls](https://github.com/Mbed-TLS/mbedtls) - TLS/encryption support (optional), for embedded system
  - [pjsip](https://github.com/pjsip/pjsip) - SIP protocol stack (optional), for test only, no code reference

## 技术栈
- c语言开发, GCC/LLVM
- 支持linux与macos，支持FreeRTOS，Zephyr、RT-Thread等RTOS

## 架构设计
- 参考arch-v3.0.md文件中的架构设计；
- 参考3rds中的libsip, librtp, libice三个库接口，调用他们实现功能；
- 内在管理， 线程管理，日志管理，使用osal目录中的抽象层;
- 每层测试代码在test.c中，依赖其它层的代码用stub方案，腹膜编译预定义DEBUG_XXX隔离，方案测试与集成；

## 项目构建
- 使用cmake构建，在build目录下，所有中间过程文件全部在build中；
- 编译的库文件在build/lib目录下；
- 测试可执行文件在build/bin目录下；
- 测试日志文件在build/log目录下；

## 项目结构
- src: 源代码目录
- include: 头文件目录
- build: 构建目录（所有构建产物）
- 3rds: 第三方库
  - media-server: https://github.com/ireader/media-server (媒体服务器库，提供SIP/RTSP/RTP/RTMP等协议支持)
  - sdk: https://github.com/ireader/sdk (基础SDK，提供AIO、原子操作、HTTP等底层功能)
  - avcodec: https://github.com/ireader/avcodec (视频编解码库，支持H.264/H.265等)
  - 3rd: https://github.com/ireader/3rd (其他第三方依赖)

## 编码规范
- 代码风格：
  - 使用clang-format进行代码格式化
- 数据结构：
  - 使用结构体代替类
  - struct xxxx{}; typedef struct xxxx xxxx_t;
- 头文件预定义：
  - 如下包裹头文件内容：
    - #ifndef __LWS_AGENT_H__
    - #define __LWS_AGENT_H__
    - // file content
    - #endif // __LWS_AGENT_H__
  - 

## 注意隐私
- 项目中包含了一些敏感信息，特别是本地目录路径，要全部改为以项目根目录为基础的相对路径

## 调测
- 所有的日志输出，都使用lws_log_info, lws_log_debug, lws_log_error等接口；
- 调用系统bash运行测试时，日志文件放到build/logs目录下，不要放到系统的/tmp目录；

