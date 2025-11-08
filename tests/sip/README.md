# SIP Integration Tests

本目录包含 lwsip 的 SIP 协议集成测试程序。

## 测试架构

```
┌─────────┐         ┌────────────┐         ┌─────────┐
│ caller  │────────▶│ sip_server │────────▶│ callee  │
└─────────┘         └────────────┘         └─────────┘
   UAC              Fake SIP Server           UAS
```

### 测试流程

1. **sip_server** - 简单的 SIP 服务器
   - 监听 SIP 端口（默认 5060）
   - 接收并转发 SIP 消息
   - 模拟 SIP 注册、呼叫建立等功能

2. **caller** - SIP 主叫测试程序（UAC）
   - 向 sip_server 注册
   - 发起呼叫到指定用户
   - 发送音频 RTP 流

3. **callee** - SIP 被叫测试程序（UAS）
   - 向 sip_server 注册
   - 接收来电
   - 接收并处理音频 RTP 流

## 使用方法

### 1. 启动 SIP 服务器

```bash
./build/tests/sip_server
```

默认监听在 `0.0.0.0:5060`

### 2. 启动被叫端（callee）

```bash
./build/tests/callee
```

注册用户：1000

### 3. 启动主叫端（caller）

```bash
./build/tests/caller
```

注册用户：1001，呼叫目标：1000

## 编译

测试程序在主项目编译时自动构建：

```bash
cd build
cmake ..
make
```

可执行文件位置：
- `build/tests/sip_server`
- `build/tests/caller`
- `build/tests/callee`

## 测试场景

### 场景 1：基本呼叫测试
1. 启动 sip_server
2. 启动 callee (用户 1000)
3. 启动 caller (用户 1001)
4. caller 自动呼叫 1000
5. callee 自动接听
6. 建立 RTP 媒体会话
7. 通话 30 秒后自动挂断

### 场景 2：注册测试
1. 启动 sip_server
2. 启动多个 callee/caller 实例
3. 验证 SIP 注册功能
4. 验证注册过期和续约

### 场景 3：媒体传输测试
1. 使用文件设备播放预录音频
2. 验证 RTP 包发送和接收
3. 验证音频编解码（PCMA/PCMU）
4. 验证 RTCP 统计

## 调试

设置环境变量启用详细日志：

```bash
export LWS_LOG_LEVEL=DEBUG
./build/tests/caller
```

## 注意事项

- sip_server 是一个简化的测试服务器，不是完整的 SIP 服务器实现
- 仅用于测试 lwsip 的 SIP 协议栈功能
- 生产环境请使用专业 SIP 服务器（如 Asterisk, FreeSWITCH）
