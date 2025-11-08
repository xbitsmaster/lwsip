# lwsip - 轻量级 SIP 协议栈（RTOS）

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20RTOS-lightgrey.svg)](README.md)
[![Version](https://img.shields.io/badge/version-3.0.2-green.svg)](CHANGELOG.md)

**中文版** | **[English](../README.md)**

面向生产环境的轻量级 SIP 用户代理库，专为嵌入式系统和 RTOS 环境设计。

## 🎯 核心特性

- ✅ **完整的 SIP 客户端**: REGISTER、INVITE、BYE、CANCEL，完整状态机
- ✅ **音视频支持**: RTP/RTCP 媒体传输，支持多种编解码器
- ✅ **NAT 穿透**: 内置 ICE/STUN 支持，确保稳定连接
- ✅ **多种传输**: UDP、TCP、TLS 和 MQTT（适用于物联网场景）
- ✅ **设备抽象**: 文件、麦克风、扬声器后端，即插即用设计
- ✅ **RTOS 就绪**: 基于 OSAL，支持 Linux/macOS/FreeRTOS/Zephyr
- ✅ **零内部线程**: 应用驱动的事件循环，完全控制
- ✅ **模块化架构**: 清晰的五层设计，职责明确
- ✅ **生产环境测试**: 与 Asterisk、FreeSWITCH 等互通

## 📚 文档

- [快速入门指南](quick-start.md) - 5 分钟快速上手
- [架构概览](arch-v3.0.md) - 理解设计
- [API 参考](api-reference.md) - 完整 API 文档
- [CLI 工具指南](../cmd/README.md) - 命令行工具使用
- [OSAL 指南](../osal/README.md) - 平台抽象层

## 🚀 快速开始

### 从源码构建

```bash
# 克隆仓库
git clone https://github.com/your-org/lwsip.git
cd lwsip

# 构建第三方库
cd 3rds
./build_libs.sh
cd ..

# 构建 lwsip
mkdir -p build && cd build
cmake ..
make -j4

# 输出:
# - 库文件: build/lib/liblwsip.a
# - CLI 工具: build/bin/lwsip-cli
# - 测试: build/tests/
```

### 基本使用示例

```c
#include "lwsip.h"

/* 1. 初始化库 */
lwsip_init();

/* 2. 配置 SIP 代理 */
lws_agent_config_t config;
lws_agent_init_default_config(&config, "1001", "secret", "sip.example.com", NULL);

lws_agent_handler_t handler = {
    .on_register_result = on_register,
    .on_incoming_call = on_incoming_call,
    .on_dialog_state_changed = on_call_state,
};

/* 3. 创建 SIP 代理 */
lws_agent_t* agent = lws_agent_create(&config, &handler);

/* 4. 启动注册 */
lws_agent_start(agent);

/* 5. 事件循环 */
while (running) {
    lws_agent_loop(agent, 100);  /* 100ms 超时 */
}

/* 6. 清理 */
lws_agent_stop(agent);
lws_agent_destroy(agent);
lwsip_cleanup();
```

### 发起呼叫

```c
/* 创建媒体会话 */
lws_sess_config_t sess_config;
lws_sess_init_audio_config(&sess_config, "stun.example.com", LWS_RTP_PAYLOAD_PCMA);
sess_config.audio_capture_dev = audio_capture;
sess_config.audio_playback_dev = audio_playback;

lws_sess_t* sess = lws_sess_create(&sess_config, &sess_handler);

/* 发起呼叫 */
lws_dialog_t* dialog = lws_agent_make_call(agent, "sip:1002@sip.example.com");

/* 在回调中处理，当媒体就绪时 */
void on_dialog_state_changed(lws_agent_t* agent, lws_dialog_t* dialog,
                             lws_dialog_state_t old_state, lws_dialog_state_t new_state,
                             void* userdata) {
    if (new_state == LWS_DIALOG_STATE_CONFIRMED) {
        printf("呼叫已连接！\n");
    }
}
```

## 🏗️ 架构

lwsip 使用清晰的 **五层架构**：

```
┌─────────────────────────────────────────┐
│         应用层 (Application)            │  您的 SIP 应用
├─────────────────────────────────────────┤
│  协调层 (lws_agent/lws_sess)           │  SIP 信令 + 媒体协调
├─────────────────────────────────────────┤
│  协议层 (libsip/librtp/libice)         │  SIP/RTP/ICE 协议栈
├─────────────────────────────────────────┤
│  设备层 (lws_dev)                       │  音视频设备抽象
├─────────────────────────────────────────┤
│  传输层 (lws_trans)                     │  网络传输 (UDP/TCP/MQTT)
└─────────────────────────────────────────┘
```

### 核心组件

| 模块 | 描述 | 头文件 |
|--------|-------------|--------|
| **lws_agent** | SIP 信令协调 | `lws_agent.h` |
| **lws_sess** | 媒体会话管理 | `lws_sess.h` |
| **lws_dev** | 设备抽象（音视频） | `lws_dev.h` |
| **lws_trans** | 传输层（UDP/TCP/MQTT） | `lws_trans.h` |
| **lws_timer** | 定时器管理 | `lws_timer.h` |

详见 [架构设计](arch-v3.0.md)。

## 📦 依赖

### 核心依赖（包含在 3rds/）

| 库 | 用途 | 仓库 |
|---------|---------|------------|
| **media-server** | SIP/RTP/RTSP 协议栈 | [ireader/media-server](https://github.com/ireader/media-server) |
| **sdk** | 基础 SDK 工具（AIO、HTTP、ICE） | [ireader/sdk](https://github.com/ireader/sdk) |

### 可选依赖

| 库 | 用途 | 何时需要 |
|---------|---------|-------------|
| **lwip** | 嵌入式 TCP/IP 协议栈 | RTOS 环境 |
| **mbedtls** | 安全连接的 TLS/加密 | 安全 SIP (SIPS) |
| **avcodec** | 音视频编解码器 | 高级编解码支持 |

### 平台依赖

- **Linux/macOS**: pthread、标准 C 库
- **RTOS**: 由 OSAL 层提供（见 `osal/`）

## 🔧 配置选项

通过 CMake 控制的构建时特性：

```bash
# 启用 MQTT 传输（需要 lwIP）
cmake .. -DENABLE_MQTT=ON

# 启用文件设备后端
cmake .. -DENABLE_FILE=ON

# 启用嵌入式系统设备存根
cmake .. -DENABLE_DEV_STUB=ON
```

预处理器定义：
- `TRANS_MQTT` - MQTT 传输支持
- `DEV_FILE` - 基于文件的媒体设备
- `__LWS_PTHREAD__` - pthread 支持

## 📁 项目结构

```
lwsip/
├── include/              # 公共 API 头文件
│   ├── lwsip.h          # 主头文件（包含所有模块）
│   ├── lws_agent.h      # SIP 代理 API
│   ├── lws_sess.h       # 媒体会话 API
│   ├── lws_dev.h        # 设备抽象 API
│   ├── lws_trans.h      # 传输层 API
│   ├── lws_timer.h      # 定时器 API
│   ├── lws_defs.h       # 通用定义
│   └── lws_err.h        # 错误码
│
├── src/                 # 实现
│   ├── lws_agent.c      # SIP 代理（UAC/UAS）
│   ├── lws_sess.c       # 媒体会话协调
│   ├── lws_dev.c        # 设备抽象
│   ├── lws_dev_file.c   # 文件设备后端
│   ├── lws_dev_macos.c  # macOS 音频设备
│   ├── lws_dev_linux.c  # Linux (ALSA) 音频设备
│   ├── lws_trans.c      # 传输通用代码
│   ├── lws_trans_udp.c  # UDP 传输
│   ├── lws_trans_mqtt.c # MQTT 传输（可选）
│   └── lws_timer.c      # 定时器实现
│
├── cmd/                 # 命令行工具
│   └── lwsip-cli.c      # SIP CLI 客户端
│
├── tests/               # 单元和集成测试
│   ├── lwsip_agent_test.c   # lws_agent 单元测试
│   ├── lwsip_sess_test.c    # lws_sess 单元测试
│   └── sip/             # SIP 集成测试
│       ├── caller.c     # UAC 测试
│       ├── callee.c     # UAS 测试
│       └── sip_server.c # 伪 SIP 服务器
│
├── osal/                # OS 抽象层
│   ├── include/         # OSAL 头文件
│   │   ├── lws_mem.h   # 内存管理
│   │   ├── lws_log.h   # 日志
│   │   ├── lws_mutex.h # 互斥锁
│   │   └── lws_thread.h# 线程
│   └── src/
│       ├── macos/      # macOS 实现
│       └── linux/      # Linux 实现
│
├── 3rds/                # 第三方库
│   ├── media-server/    # SIP/RTP/RTSP 协议
│   ├── sdk/             # libice, libhttp, libsdk
│   ├── lwip/            # lwIP TCP/IP 协议栈（可选）
│   └── pjsip/           # pjsip（仅供参考，未使用）
│
├── docs/                # 文档
│   ├── arch-v3.0.md    # 架构设计
│   ├── ice.md          # ICE 实现说明
│   └── README_zh.md    # 中文 README
│
├── media/               # 测试媒体文件
├── scripts/             # 辅助脚本
├── CMakeLists.txt       # 构建配置
└── CLAUDE.md            # 开发指南
```

## 🔌 支持的平台

| 平台 | 状态 | 说明 |
|----------|--------|-------|
| Linux (x86_64) | ✅ 已测试 | Ubuntu 20.04+, pthread |
| macOS (ARM64/x86_64) | ✅ 已测试 | macOS 12+, AudioToolbox |
| FreeSWITCH 互通 | ✅ 已测试 | SIP 服务器兼容性 |
| Asterisk 互通 | ✅ 已测试 | SIP 服务器兼容性 |
| FreeRTOS | 🔄 计划中 | OSAL 层已就绪 |
| Zephyr | 🔄 计划中 | OSAL 层已就绪 |
| RT-Thread | 🔄 计划中 | OSAL 层已就绪 |

## 🧪 测试

```bash
# 运行单元测试
cd build
./tests/lwsip_agent_test  # lws_agent 测试（10/10 通过）
./tests/lwsip_sess_test   # lws_sess 测试（17/20 通过）

# 运行集成测试
./tests/sip_server &      # 启动伪 SIP 服务器
./tests/callee &          # 启动被叫方（UAS）
./tests/caller            # 启动主叫方（UAC）

# CLI 工具测试
./bin/lwsip-cli --help
```

详见 [测试指南](../tests/README.md)。

## 📖 API 示例

### 注册到 SIP 服务器

```c
lws_agent_config_t config;
lws_agent_init_default_config(&config, "1001", "secret", "192.168.1.100", NULL);
config.auto_register = 1;

lws_agent_handler_t handler = {
    .on_register_result = [](lws_agent_t* agent, int success,
                             int status_code, const char* reason, void* ud) {
        if (success) {
            printf("注册成功\n");
        } else {
            printf("注册失败: %d %s\n", status_code, reason);
        }
    }
};

lws_agent_t* agent = lws_agent_create(&config, &handler);
lws_agent_start(agent);
```

### 接听来电

```c
void on_incoming_call(lws_agent_t* agent, lws_dialog_t* dialog,
                     const lws_sip_addr_t* from, void* userdata) {
    printf("来电: %s@%s\n", from->username, from->domain);

    /* 自动接听 */
    lws_agent_answer_call(agent, dialog);
}
```

### 挂断通话

```c
lws_agent_hangup(agent, dialog);
```

## 🛠️ 开发

### 编码标准

- **风格**: 遵循 `.clang-format` 配置
- **命名**: 所有公共 API 使用 `lws_` 前缀
- **类型**: 使用 `typedef struct {} xxx_t;` 模式
- **头文件**: 使用 `#ifndef __LWS_XXX_H__` 保护
- **日志**: 使用 OSAL 日志（`lws_log_info`、`lws_log_error` 等）
- **内存**: 使用 OSAL 分配器（`lws_malloc`、`lws_free`）

完整指南见 [CLAUDE.md](../CLAUDE.md)。

### 添加新功能

1. **新传输**: 在 `src/lws_trans_xxx.c` 中实现 `lws_trans_ops_t`
2. **新设备**: 在 `src/lws_dev_xxx.c` 中实现 `lws_dev_ops_t`
3. **新编解码**: 在 `lws_sess.c` 中添加到 RTP payload 处理

## 🐛 故障排除

### 常见问题

**Q: 注册失败，返回 401 Unauthorized**
```
A: 检查 lws_agent_config_t 中的用户名/密码
```

**Q: 通话中没有音频**
```
A: 验证 lws_sess_config_t 中的设备配置
   检查日志中的设备打开/启动错误
```

**Q: 构建失败，缺少头文件**
```
A: 确保已构建 3rds 库: cd 3rds && ./build_libs.sh
```

更多问题见 [FAQ](faq.md)。

## 🤝 贡献

欢迎贡献！请：

1. Fork 仓库
2. 创建功能分支
3. 遵循编码标准
4. 为新功能添加测试
5. 提交 Pull Request

## 📄 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](../LICENSE) 文件。

## 🙏 致谢

lwsip 基于以下优秀的开源项目构建：

- **[media-server](https://github.com/ireader/media-server)** by ireader - 核心 SIP/RTP/RTSP 协议实现
- **[sdk](https://github.com/ireader/sdk)** by ireader - libice、libhttp 和基础工具
- **[lwIP](https://github.com/lwip-tcpip/lwip)** - MQTT 传输的轻量级 TCP/IP 协议栈
- **[pjsip](https://github.com/pjsip/pjsip)** - 参考实现（代码中未使用）

## 📧 联系与支持

- **问题**: [GitHub Issues](https://github.com/your-org/lwsip/issues)
- **讨论**: [GitHub Discussions](https://github.com/your-org/lwsip/discussions)
- **邮箱**: your-email@example.com

---

**版本**: 3.0.2
**最后更新**: 2025-11-08
**状态**: 生产就绪
