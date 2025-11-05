# lwsip - Lightweight SIP Stack for RTOS

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20RTOS-lightgrey.svg)](README.md)

中文版 | **[English](../README.md)**

轻量级SIP客户端协议栈，专为嵌入式系统和RTOS设计。

## 特性

- ✅ **完整的SIP UAC功能**: REGISTER, INVITE, BYE, CANCEL, UNREGISTER
- ✅ **RTP/RTCP媒体传输**: 支持音频和视频流
- ✅ **ICE NAT穿透**: 集成STUN/TURN支持
- ✅ **灵活的传输层**: TCP/UDP, MQTT等可扩展传输方式
- ✅ **跨平台抽象**: 基于OSAL，支持Linux/macOS/RTOS
- ✅ **Worker线程支持**: 可选的后台事件处理
- ✅ **零拷贝设计**: 高效的媒体数据处理
- ✅ **模块化架构**: SIP/RTP/Media层次清晰分离

## 快速开始

### 依赖

- GCC/Clang编译器
- CMake 3.10+
- pthread库（Linux/macOS）
- 第三方库（已包含在3rds目录）：
  - [media-server](https://github.com/ireader/media-server) - SIP/RTP协议实现
  - [sdk](https://github.com/ireader/sdk) - 基础SDK工具
  - [avcodec](https://github.com/ireader/avcodec) - 音视频编解码
  - [lwip](https://github.com/lwip-tcpip/lwip) - TCP/IP协议栈（可选）
  - [mbedtls](https://github.com/Mbed-TLS/mbedtls) - TLS/加密支持（可选）

### 构建

```bash
# 1. 克隆项目
git clone <repository-url>
cd lwsip

# 2. 构建（使用脚本）
./build.sh

# 3. 或手动构建
mkdir -p build && cd build
cmake ..
make

# 构建产物
# - 静态库: build/lib/liblwsip.a
# - CLI工具: build/bin/lwsip-cli
```

### 清理

```bash
./clean.sh
```

## 项目结构

```
lwsip/
├── include/              # 公共头文件
│   ├── lws_client.h     # SIP客户端核心接口
│   ├── lws_uac.h        # User Agent Client
│   ├── lws_uas.h        # User Agent Server
│   ├── lws_session.h    # RTP会话管理
│   ├── lws_payload.h    # RTP payload封装
│   ├── lws_media.h      # 媒体源/目标抽象
│   ├── lws_transport.h  # 传输层抽象
│   ├── lws_ice.h        # ICE NAT穿透
│   ├── lws_types.h      # 类型定义
│   └── lws_error.h      # 错误码定义
│
├── src/                 # 实现文件
│   ├── lws_client.c     # SIP客户端实现
│   ├── lws_uac.c        # UAC实现
│   ├── lws_uas.c        # UAS实现
│   ├── lws_session.c    # RTP会话实现
│   ├── lws_payload.c    # Payload封装实现
│   ├── lws_media.c      # 媒体I/O实现
│   ├── lws_transport_tcp.c  # TCP/UDP传输
│   ├── lws_transport_mqtt.c # MQTT传输
│   ├── lws_ice.c        # ICE实现
│   ├── lws_sip_timer.c  # SIP定时器
│   └── lws_error.c      # 错误码映射
│
├── cmd/                 # 命令行工具
│   └── lwsip_cli.c      # CLI测试工具
│
├── osal/                # OS抽象层
│   ├── include/         # OSAL头文件
│   └── src/             # 平台相关实现
│       ├── linux/       # Linux实现
│       └── macos/       # macOS实现
│
├── 3rds/                # 第三方依赖库
│   ├── media-server/    # SIP/RTP协议栈
│   ├── sdk/             # 基础SDK
│   ├── avcodec/         # 音视频编解码
│   ├── lwip/            # TCP/IP协议栈
│   └── mbedtls/         # TLS/加密库
│
├── media/               # 测试媒体文件
├── scripts/             # 辅助脚本
├── build.sh             # 构建脚本
├── clean.sh             # 清理脚本
└── CMakeLists.txt       # CMake配置
```

## 使用示例

### 1. 创建SIP客户端

```c
#include "lws_client.h"

// 配置客户端
lws_config_t config = {
    .server_host = "192.168.1.100",
    .server_port = 5060,
    .local_port = 5080,
    .username = "1002",
    .password = "1234",
    .realm = "asterisk",
    .enable_audio = 1,
    .enable_video = 0,
    .audio_codec = LWS_AUDIO_CODEC_PCMU,
    .use_worker_thread = 1,  // 使用后台线程
};

// 设置回调
lws_client_handler_t handler = {
    .on_reg_state = on_reg_state,
    .on_call_state = on_call_state,
    .on_incoming_call = on_incoming_call,
    .on_error = on_error,
    .param = NULL,
};

// 创建客户端
lws_client_t* client = lws_client_create(&config, &handler);
if (!client) {
    fprintf(stderr, "Failed to create client\n");
    return -1;
}

// 启动客户端
lws_client_start(client);
```

### 2. 注册到SIP服务器

```c
// 发起注册
int ret = lws_uac_register(client);
if (ret != 0) {
    fprintf(stderr, "Register failed: %s\n", lws_error_string(ret));
}

// 在回调中处理注册状态
void on_reg_state(void* param, lws_reg_state_t state, int code) {
    switch (state) {
    case LWS_REG_REGISTERED:
        printf("Registered successfully\n");
        break;
    case LWS_REG_UNREGISTERED:
        printf("Unregistered\n");
        break;
    case LWS_REG_FAILED:
        printf("Registration failed: %d\n", code);
        break;
    }
}
```

### 3. 发起呼叫

```c
// 一行代码发起呼叫
lws_session_t* session = lws_call(client, "sip:1001@192.168.1.100");
if (!session) {
    fprintf(stderr, "Failed to initiate call\n");
    return -1;
}

// 处理呼叫状态
void on_call_state(void* param, const char* peer, lws_call_state_t state) {
    switch (state) {
    case LWS_CALL_STATE_RINGING:
        printf("Ringing...\n");
        break;
    case LWS_CALL_STATE_CONNECTED:
        printf("Call connected\n");
        break;
    case LWS_CALL_STATE_TERMINATED:
        printf("Call terminated\n");
        break;
    }
}
```

### 4. 接听来电

```c
void on_incoming_call(void* param, const char* from,
                      const char* to, const char* sdp, int sdp_len) {
    printf("Incoming call from: %s\n", from);

    // 接听
    lws_session_t* session = lws_answer(client, from);

    // 或拒绝
    // lws_uas_reject(client, from, 486);  // Busy Here
}
```

### 5. 挂断呼叫

```c
// 挂断当前呼叫
int ret = lws_hangup(client, session);
if (ret != 0) {
    fprintf(stderr, "Hangup failed: %s\n", lws_error_string(ret));
}
```

### 6. 事件循环

```c
// 方式1: 手动事件循环（不使用worker线程）
while (running) {
    // 处理SIP事件（100ms超时）
    int ret = lws_client_loop(client, 100);
    if (ret < 0) {
        fprintf(stderr, "Error: %s\n", lws_error_string(ret));
        break;
    }
}

// 方式2: 使用worker线程（在config中设置use_worker_thread=1）
// 客户端会自动在后台处理事件，无需手动调用lws_client_loop
```

### 7. 清理资源

```c
// 注销
lws_uac_unregister(client);

// 停止客户端
lws_client_stop(client);

// 销毁客户端
lws_client_destroy(client);
```

## CLI工具使用

项目提供了命令行测试工具 `lwsip-cli`：

```bash
# 基本用法
./build/bin/lwsip-cli [options]

# 详细使用说明见 cmd/README.md
```

## OSAL (OS Abstraction Layer)

lwsip使用OSAL实现跨平台支持，详见 [osal/README.md](osal/README.md)

支持的平台：
- ✅ Linux (pthread)
- ✅ macOS (pthread + os_unfair_lock)
- 🔄 FreeRTOS (计划中)
- 🔄 Zephyr (计划中)
- 🔄 RT-Thread (计划中)

## API文档

### 核心API

- `lws_client_create()` - 创建SIP客户端
- `lws_client_start()` - 启动客户端
- `lws_client_stop()` - 停止客户端
- `lws_client_destroy()` - 销毁客户端
- `lws_client_loop()` - 事件循环（手动模式）

### UAC API

- `lws_uac_register()` - 发起注册
- `lws_uac_unregister()` - 注销
- `lws_call()` - 发起呼叫（简化API）
- `lws_hangup()` - 挂断呼叫（简化API）

### UAS API

- `lws_answer()` - 接听来电（简化API）
- `lws_uas_reject()` - 拒绝来电

### Session API

- `lws_session_create()` - 创建RTP会话
- `lws_session_start()` - 启动会话
- `lws_session_stop()` - 停止会话
- `lws_session_destroy()` - 销毁会话
- `lws_session_poll()` - 轮询RTP/RTCP数据
- `lws_session_set_dialog()` - 设置SIP dialog
- `lws_session_get_dialog()` - 获取SIP dialog

## 错误处理

所有API返回整数错误码，使用 `lws_error_string()` 获取错误描述：

```c
int ret = lws_call(client, peer);
if (ret < 0) {
    fprintf(stderr, "Error: %s (0x%08x)\n",
            lws_error_string(ret), ret);
}
```

错误码定义见 `include/lws_error.h`

## 测试

### 与FreeSWITCH互通测试

参考 `scripts/freeswitch/README.md` 配置FreeSWITCH测试环境。

## 开发指南

- 编码规范: 见 `CLAUDE.md`
- 命名约定: 所有公共API使用 `lws_` 前缀
- 结构体定义: `typedef struct xxxx {} xxxx_t;`
- 头文件保护: `#ifndef __LWS_XXX_H__`
- 代码格式化: 使用 clang-format

## 贡献

欢迎提交Issue和Pull Request。

## 许可证

[MIT License](LICENSE)

## 致谢

本项目基于以下开源项目：
- [media-server](https://github.com/ireader/media-server) - SIP/RTP/RTSP协议实现
- [sdk](https://github.com/ireader/sdk) - 基础SDK工具库
- [avcodec](https://github.com/ireader/avcodec) - 音视频编解码库
- [lwip](https://github.com/lwip-tcpip/lwip) - 轻量级TCP/IP协议栈
- [mbedtls](https://github.com/Mbed-TLS/mbedtls) - TLS/加密库

## 联系方式

- Issues: GitHub Issues
- Email: <your-email>

---

**Version**: v1.0.0
**Last Updated**: 2025-11-05
