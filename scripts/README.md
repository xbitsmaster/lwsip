# LWSIP 测试脚本

本目录包含用于测试 LWSIP SIP 客户端的脚本。

## 目录结构

```
scripts/
├── pjsip_callee.sh          # PJSIP 被叫方脚本
├── pjsip_caller.sh          # PJSIP 主叫方脚本
├── freeswitch/              # FreeSWITCH Docker 部署
│   ├── install.sh
│   ├── start.sh
│   ├── stop.sh
│   ├── uninstall.sh
│   └── README.md
└── README.md                # 本文档
```

## PJSIP 测试脚本

### 被叫方脚本 (pjsip_callee.sh)

启动一个 PJSIP 客户端作为被叫方，自动接听来电。

**用法**:
```bash
./pjsip_callee.sh <扩展号> [选项]

参数:
  <扩展号>        SIP 扩展号

选项:
  -p, --port      本地 SIP 端口 (默认: 5070)
  -d, --duration  运行时长（秒），0 表示无限期 (默认: 0)
  -h, --help      显示帮助信息
```

**示例**:
```bash
# 启动被叫方，监听端口 5070
./pjsip_callee.sh 1001

# 使用自定义端口
./pjsip_callee.sh 1002 -p 6000

# 运行 60 秒后自动退出
./pjsip_callee.sh 1003 -d 60
```

### 主叫方脚本 (pjsip_caller.sh)

启动一个 PJSIP 客户端作为主叫方，拨打电话并播放音频文件。

**用法**:
```bash
./pjsip_caller.sh <本地扩展号> <目标号码> [选项]

参数:
  <本地扩展号>    本地 SIP 扩展号
  <目标号码>      要拨打的目标号码

选项:
  -p, --port      本地 SIP 端口 (默认: 5080)
  -d, --duration  通话时长（秒） (默认: 30)
  -a, --audio     音频文件路径
  -h, --help      显示帮助信息
```

**示例**:
```bash
# 从 1000 呼叫 1001
./pjsip_caller.sh 1000 1001

# 呼叫 60 秒
./pjsip_caller.sh 1000 1002 -d 60

# 使用指定音频文件
./pjsip_caller.sh 1000 1003 -a ../media/test_audio_pcma.wav
```

## 点对点测试（不使用 FreeSWITCH）

最简单的测试方法是直接在两个终端中运行 pjsip 脚本，进行点对点呼叫。

### 步骤 1: 启动被叫方

在**终端 1**中运行：

```bash
cd scripts
./pjsip_callee.sh 1001
```

这将启动一个 PJSIP 客户端，监听在端口 5070，等待来电。

### 步骤 2: 启动主叫方

等待被叫方完全启动后（约 3 秒），在**终端 2**中运行：

```bash
cd scripts
./pjsip_caller.sh 1000 1001
```

主叫方会拨打电话到被叫方，并播放音频文件。

### 预期结果

- 被叫方会自动接听来电
- 主叫方会播放音频文件（循环播放）
- 被叫方会接收音频数据（统计后丢弃）
- 通话默认持续 30 秒后自动挂断

## 使用 FreeSWITCH 测试

如果需要通过 FreeSWITCH 服务器进行测试，请参考 [freeswitch/README.md](freeswitch/README.md)。

### 快速开始

```bash
# 1. 安装并启动 FreeSWITCH
cd freeswitch
./install.sh
./start.sh

# 2. 在终端 1 启动被叫方
cd ..
./pjsip_callee.sh 1001

# 3. 在终端 2 启动主叫方
./pjsip_caller.sh 1000 1001
```

## 音频文件

测试脚本使用 `media/` 目录中的音频文件：

- `test_audio_pcmu.wav` - G.711 μ-law 编码（默认）
- `test_audio_pcma.wav` - G.711 A-law 编码

## 故障排查

### 端口已被占用

如果看到 "Address already in use" 错误：

```bash
# 检查端口占用
lsof -i :5070

# 使用不同的端口
./pjsip_callee.sh 1001 -p 6000
```

### 无法建立呼叫

1. 确认被叫方已经启动并就绪
2. 检查防火墙设置
3. 确认两个客户端使用不同的端口
4. 查看日志文件 /tmp/callee_*.log 和 /tmp/caller_*.log

### PJSUA 崩溃

如果 PJSUA 意外退出：

1. 检查音频文件是否存在
2. 确认 PJSUA 可执行文件路径正确
3. 尝试减少通话时长

## 技术细节

### PJSIP 参数

- `--null-audio`: 使用空音频设备，不需要实际的音频硬件
- `--auto-answer 200`: 自动以 200 OK 响应接听来电
- `--play-file`: 播放指定的音频文件
- `--auto-play`: 自动播放
- `--auto-loop`: 循环播放
- `--no-tcp`: 仅使用 UDP 传输
- `--max-calls 1`: 最多 1 个并发通话

### 编解码器

脚本使用 PCMU (G.711 μ-law) 编解码器，这是最常用的 SIP 编解码器之一。

### 网络

- 被叫方默认监听端口: 5070
- 主叫方默认监听端口: 5080
- RTP 端口: 自动分配

## 相关文档

- [FreeSWITCH 部署文档](freeswitch/README.md)
- [PJSIP 官方文档](https://docs.pjsip.org/)

## 许可证

本项目遵循项目根目录的许可证。
