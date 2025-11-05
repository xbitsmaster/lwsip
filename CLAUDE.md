
## lwsip
lwsip = light weight sip stack for RTOS
用于嵌入式系统的SIP客户端开源软件
基于https://github.com/ireader/media-server开源项目开发

## 技术栈
- c语言开发, GCC/LLVM
- 支持linux与macos，支持FreeRTOS，Zephyr、RT-Thread等RTOS
- 

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
    - #ifndef __LWS_CLIENT_H__
    - #define __LWS_CLIENT_H__
    - // file content
    - #endif // __LWS_CLIENT_H__
  - 

