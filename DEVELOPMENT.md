# Lwsip Development Guide

## 项目结构

```
lwsip/
├── include/              # 公共头文件
│   ├── lws_client.h  # 主客户端API
│   ├── lws_types.h   # 类型定义
│   ├── sip_transport.h  # SIP传输层
│   └── media_session.h  # 媒体会话管理
├── src/                  # 源代码
│   ├── main.c           # 主程序
│   ├── lws_client.c  # SIP客户端核心
│   └── sip_transport.c  # SIP传输实现
├── 3rds/                 # 第三方库
│   ├── media-server/    # SIP/RTP/RTSP协议栈
│   ├── sdk/             # 基础SDK
│   ├── avcodec/         # 编解码库
│   └── 3rd/             # 其他依赖
└── build/                # 构建目录

```

## 核心模块

### 1. SIP客户端核心 (lws_client.c)

**功能**:
- SIP注册/注销
- SIP呼叫管理
- 事件回调处理

**主要函数**:
- `lws_client_create()` - 创建客户端实例
- `lws_register()` - 注册到SIP服务器
- `lws_call()` - 发起呼叫
- `lws_answer()` - 接听呼叫
- `lws_hangup()` - 挂断呼叫

### 2. SIP传输层 (sip_transport.c)

**功能**:
- UDP/TCP socket管理
- SIP消息发送/接收
- Via头生成

**核心结构**:
```c
struct sip_transport_ctx_t {
    int udp_socket;
    int tcp_socket;
    struct sockaddr_storage addr;
    struct sip_transport_t handler;
};
```

### 3. 媒体会话 (media_session.h)

**功能**:
- RTP音视频传输
- SDP协商
- 编解码管理

**待实现**.

## 编译构建

### 1. 构建依赖库

首先需要构建media-server及其依赖:

```bash
# 构建SDK
cd 3rds/sdk
make clean && make

# 构建media-server
cd ../media-server
make clean && make

# 或使用项目Makefile
cd ../..
make deps
```

### 2. 构建lwsip

```bash
# 使用构建脚本
./build.sh

# 或使用make
make lwsip

# 或手动
mkdir -p build
cd build
cmake ..
make
```

## 使用示例

### 基本用法

```bash
# 默认配置运行
./build/bin/lwsip

# 指定SIP服务器
./build/bin/lwsip -s 192.168.1.100 -u 1001 -w secret

# 注册后拨打电话
./build/bin/lwsip -c sip:1002@192.168.1.100
```

### 命令行参数

- `-h` - 显示帮助
- `-s <server>` - SIP服务器地址
- `-p <port>` - SIP服务器端口 (默认5060)
- `-u <user>` - SIP用户名
- `-w <pass>` - SIP密码
- `-c <peer>` - 拨打指定URI

### 交互命令

程序运行后可用命令:
- `r` - 重新注册
- `u` - 注销
- `q` - 退出

## API使用

### 创建客户端

```c
#include "lws_client.h"

// 配置
lws_config_t config = {
    .server_host = "192.168.1.100",
    .server_port = 5060,
    .username = "1001",
    .password = "secret",
    .register_expires = 300,
    .use_tcp = 0
};

// 回调
lws_callbacks_t callbacks = {
    .on_reg_state = on_reg_state,
    .on_call_state = on_call_state,
    .on_incoming_call = on_incoming_call
};

// 创建
lws_client_t* client = lws_client_create(&config, &callbacks);
```

### 启动和注册

```c
// 启动
lws_client_start(client);

// 注册
lws_register(client);
```

### 拨打电话

```c
lws_session_t* session = lws_call(client, "sip:1002@192.168.1.100");
```

## 当前实现状态

### ✅ 已完成

- [x] SIP传输层 (UDP)
- [x] SIP注册/注销
- [x] HTTP Digest认证
- [x] 基本事件回调
- [x] 命令行界面

### ⏳ 进行中

- [ ] SIP呼叫 (INVITE/ACK/BYE)
- [ ] RTP音视频传输
- [ ] SDP协商
- [ ] 媒体编解码

### 📋 计划中

- [ ] TCP传输支持
- [ ] ICE/STUN/TURN
- [ ] 多路呼叫
- [ ] DTMF支持
- [ ] 呼叫转移
- [ ] 会议功能

## 调试

### 编译调试版本

```bash
./build.sh Debug
```

### 查看SIP消息

SIP消息会打印到控制台，可以看到完整的注册和呼叫流程。

### 常见问题

1. **编译错误: 找不到头文件**
   - 确保已构建依赖库: `make deps`
   - 检查符号链接是否正确创建

2. **注册失败**
   - 检查SIP服务器地址和端口
   - 验证用户名和密码
   - 查看服务器日志

3. **无法接收消息**
   - 检查防火墙设置
   - 确认UDP端口5060未被占用

## 扩展开发

### 添加新的编解码器

在 `media_session.c` 中:

```c
int media_session_init_audio(media_session_t* session,
                               lws_audio_codec_t codec,
                               int sample_rate,
                               int channels)
{
    // 添加新编解码器支持
}
```

### 实现呼叫功能

参考 `sip-uac-test2.cpp`:
1. 创建INVITE事务
2. 生成SDP
3. 处理200 OK响应
4. 发送ACK
5. 启动RTP传输

## 参考资料

- [RFC 3261 - SIP协议](https://tools.ietf.org/html/rfc3261)
- [RFC 3550 - RTP协议](https://tools.ietf.org/html/rfc3550)
- [RFC 4566 - SDP协议](https://tools.ietf.org/html/rfc4566)
- [media-server文档](https://github.com/ireader/media-server)
