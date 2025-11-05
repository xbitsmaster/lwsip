# lwsip-cli - Command Line Interface

lwsip项目的命令行测试工具，用于快速测试SIP客户端功能。

## 功能特性

- ✅ SIP注册/注销
- ✅ 发起呼叫
- ✅ 接听来电
- ✅ 拒绝来电
- ✅ 挂断呼叫
- ✅ 交互式命令行界面
- ✅ 实时状态回调显示

## 构建

lwsip-cli作为主项目的一部分自动构建：

```bash
# 在lwsip根目录
./build.sh

# 或手动构建
cd build
cmake ..
make

# 构建产物
# - 可执行文件: build/bin/lwsip-cli
```

## 使用方法

### 基本用法

```bash
./build/bin/lwsip-cli <server> <username> [password]
```

**参数说明**:
- `<server>` - SIP服务器IP地址或域名
- `<username>` - SIP账号用户名
- `[password]` - SIP账号密码（可选，默认使用用户名作为密码）

### 示例

```bash
# 使用用户名作为密码
./build/bin/lwsip-cli 192.168.1.100 1002

# 指定密码
./build/bin/lwsip-cli 192.168.1.100 1002 1234

# 使用域名
./build/bin/lwsip-cli sip.example.com 1002 mypassword
```

## 默认配置

lwsip-cli使用以下默认配置：

- **SIP服务器端口**: 5060
- **本地端口**: 自动分配
- **传输协议**: UDP
- **注册过期时间**: 300秒
- **音频**: 启用（PCMU编解码）
- **视频**: 禁用

## 交互式命令

启动后，可以使用以下命令：

### call - 发起呼叫

```
> call <uri>
```

发起到指定URI的呼叫。

**示例**:
```
> call sip:1001@192.168.1.100
> call 1001@192.168.1.100
> call 1001
```

### answer - 接听来电

```
> answer
```

接听当前来电。

**示例**:
```
[INCOMING] sip:1001@192.168.1.100 -> sip:1002@192.168.1.100 (SDP: 256 bytes)
Type 'answer' to accept or 'reject' to decline
> answer
Answered call
```

### reject - 拒绝来电

```
> reject
```

拒绝当前来电（返回486 Busy Here）。

**示例**:
```
[INCOMING] sip:1001@192.168.1.100 -> sip:1002@192.168.1.100 (SDP: 256 bytes)
Type 'answer' to accept or 'reject' to decline
> reject
Rejected call
```

### hangup - 挂断呼叫

```
> hangup
```

挂断当前活动的呼叫。

**示例**:
```
> hangup
Hung up
```

### quit/exit - 退出程序

```
> quit
```

或

```
> exit
```

退出lwsip-cli程序。

### help - 显示帮助

```
> help
```

或

```
> ?
```

显示所有可用命令。

## 状态回调

lwsip-cli会实时显示SIP状态变化：

### 注册状态

```
[REG] REGISTERING (code: 0)
[REG] REGISTERED (code: 200)
[REG] UNREGISTERED (code: 200)
[REG] FAILED (code: 401)
```

### 呼叫状态

```
[CALL] sip:1001@192.168.1.100 - CALLING
[CALL] sip:1001@192.168.1.100 - RINGING
[CALL] sip:1001@192.168.1.100 - ESTABLISHED
[CALL] sip:1001@192.168.1.100 - TERMINATED
```

### 来电通知

```
[INCOMING] sip:1001@192.168.1.100 -> sip:1002@192.168.1.100 (SDP: 256 bytes)
Type 'answer' to accept or 'reject' to decline
```

### 错误消息

```
[ERROR] 0x00000001: Out of memory
[ERROR] 0x00000100: SIP registration failed
```

## 使用场景示例

### 场景1: 简单呼叫测试

```bash
# 终端1: 启动1002账号
./build/bin/lwsip-cli 192.168.1.100 1002 1234
[REG] REGISTERED (code: 200)
> call 1001
[CALL] sip:1001@192.168.1.100 - CALLING
[CALL] sip:1001@192.168.1.100 - RINGING
[CALL] sip:1001@192.168.1.100 - ESTABLISHED

# 通话中...

> hangup
[CALL] sip:1001@192.168.1.100 - TERMINATED
```

### 场景2: 接听来电测试

```bash
# 终端2: 启动1001账号
./build/bin/lwsip-cli 192.168.1.100 1001 1234
[REG] REGISTERED (code: 200)

# 等待来电...
[INCOMING] sip:1002@192.168.1.100 -> sip:1001@192.168.1.100 (SDP: 256 bytes)
Type 'answer' to accept or 'reject' to decline

> answer
Answered call
[CALL] sip:1002@192.168.1.100 - ESTABLISHED

# 通话中...
```

### 场景3: 拒绝来电

```bash
./build/bin/lwsip-cli 192.168.1.100 1001 1234
[REG] REGISTERED (code: 200)

[INCOMING] sip:1002@192.168.1.100 -> sip:1001@192.168.1.100 (SDP: 256 bytes)
Type 'answer' to accept or 'reject' to decline

> reject
Rejected call
```

## 退出

使用以下任一方式退出：

1. 输入命令: `quit` 或 `exit`
2. 按 `Ctrl+C`
3. 发送 `SIGTERM` 信号

退出时会自动：
1. 挂断活动呼叫
2. 注销SIP账号
3. 清理所有资源

## 限制

当前版本的限制：

- 仅支持单个并发呼叫
- 音频编解码固定为PCMU
- 不支持视频
- 不支持TCP传输
- 不支持TLS加密

这些限制将在未来版本中改进。

## 故障排除

### 注册失败

```
[REG] FAILED (code: 401)
```

**原因**: 认证失败

**解决**:
- 检查用户名和密码是否正确
- 确认SIP服务器配置正确

### 连接超时

```
[ERROR] 0x00000005: Operation timeout
```

**原因**: 无法连接到SIP服务器

**解决**:
- 检查服务器IP地址是否正确
- 确认网络连接正常
- 检查防火墙设置

### 无法呼叫

```
[CALL] sip:1001@192.168.1.100 - FAILED
```

**原因**: 呼叫失败

**解决**:
- 确认已成功注册
- 检查对方账号是否存在
- 查看对方是否在线

## 调试

如需详细日志，可以修改源代码中的日志级别，或使用strace/tcpdump等工具：

```bash
# 使用tcpdump抓包SIP消息
sudo tcpdump -i any -n port 5060 -A

# 使用strace跟踪系统调用
strace -f ./build/bin/lwsip-cli 192.168.1.100 1002 1234
```

## 源代码

CLI工具源代码位于 `cmd/lwsip_cli.c`。

主要功能模块：
- 命令解析器
- 状态回调处理
- 信号处理
- 主事件循环

## 与FreeSWITCH互通测试

参考 [../scripts/freeswitch/README.md](../scripts/freeswitch/README.md) 配置FreeSWITCH环境，然后使用lwsip-cli进行互通测试。

## 开发和扩展

如需添加新命令或功能：

1. 在 `process_command()` 函数中添加命令处理逻辑
2. 在 `help` 命令中添加说明
3. 重新编译

示例：添加 `mute` 命令

```c
else if (strcmp(command, "mute") == 0) {
    // TODO: 实现静音功能
    printf("Muted\n");
}
```

## 许可证

与主项目lwsip保持一致。

---

**Version**: 1.0.0
**Last Updated**: 2025-11-05
