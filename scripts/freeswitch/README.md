# FreeSWITCH 本地 Docker 部署

本目录包含用于在本地环境部署 FreeSWITCH 的 Docker 脚本和配置文件。

## 目录结构

```
scripts/freeswitch/
├── install.sh              # 安装脚本
├── uninstall.sh            # 卸载脚本
├── start.sh                # 启动脚本
├── stop.sh                 # 停止脚本
├── docker-compose.yml      # Docker Compose 配置
├── config/                 # FreeSWITCH 配置目录
│   └── directory/          # SIP 用户配置
│       ├── 1000.xml        # 用户 1000
│       ├── 1001.xml        # 用户 1001
│       ├── 1002.xml        # 用户 1002
│       └── 1003.xml        # 用户 1003
└── README.md               # 本文档
```

## 前置要求

- Docker (推荐 20.10+)
- Docker Compose (推荐 v2.0+)
- macOS / Linux 操作系统

## 快速开始

### 1. 安装

```bash
cd scripts/freeswitch
./install.sh
```

安装脚本会执行以下操作：
- 检查 Docker 环境
- 验证配置文件
- 拉取 FreeSWITCH Docker 镜像
- 显示配置信息

### 2. 启动服务

```bash
./start.sh
```

启动脚本会：
- 启动 FreeSWITCH 容器
- 等待服务就绪
- 显示服务状态和使用信息

### 3. 停止服务

```bash
./stop.sh
```

停止脚本会：
- 检查当前运行状态
- 显示活动连接和通话
- 优雅停止容器（如有活动通话会提示确认）

### 4. 卸载

```bash
./uninstall.sh
```

卸载脚本会：
- 停止并删除容器
- 删除 Docker volumes
- 删除 Docker networks
- 可选删除镜像

## 服务配置

### 网络配置

- **服务器地址**: 127.0.0.1 (本地回环)
- **SIP 端口**: 5060 (TCP/UDP)
- **备用 SIP 端口**: 5080 (TCP/UDP)
- **RTP 端口范围**: 16384-16394 (UDP)
- **ESL 端口**: 8021 (TCP)

### SIP 用户账号

系统预配置了 4 个测试账号，密码均为 `1234`：

| 账号 | SIP URI | 密码 | 描述 |
|------|---------|------|------|
| 1000 | sip:1000@127.0.0.1 | 1234 | Extension 1000 |
| 1001 | sip:1001@127.0.0.1 | 1234 | Extension 1001 |
| 1002 | sip:1002@127.0.0.1 | 1234 | Extension 1002 |
| 1003 | sip:1003@127.0.0.1 | 1234 | Extension 1003 |

## 使用示例

### 查看容器日志

```bash
cd scripts/freeswitch
docker compose logs -f
```

### 进入 FreeSWITCH 控制台

```bash
docker exec -it freeswitch-local fs_cli
```

控制台常用命令：
- `status` - 查看服务状态
- `sofia status` - 查看 SIP 配置
- `show registrations` - 查看注册用户
- `show channels` - 查看活动通话
- `version` - 查看版本信息

### 查看 SIP 注册

```bash
docker exec freeswitch-local fs_cli -x "show registrations"
```

### 查看活动通话

```bash
docker exec freeswitch-local fs_cli -x "show channels"
```

### 测试 SIP 注册（使用 pjsua）

```bash
# 从项目根目录运行
./3rds/pjsip/pjsip-apps/bin/pjsua-aarch64-apple-darwin24.6.0 \
  --id sip:1000@127.0.0.1 \
  --registrar sip:127.0.0.1 \
  --realm "*" \
  --username 1000 \
  --password 1234 \
  --null-audio
```

### 测试 SIP 呼叫

**终端 1 - 被叫方 (1001)**:
```bash
./3rds/pjsip/pjsip-apps/bin/pjsua-aarch64-apple-darwin24.6.0 \
  --id sip:1001@127.0.0.1 \
  --registrar sip:127.0.0.1 \
  --realm "*" \
  --username 1001 \
  --password 1234 \
  --null-audio \
  --auto-answer 200
```

**终端 2 - 主叫方 (1000)**:
```bash
./3rds/pjsip/pjsip-apps/bin/pjsua-aarch64-apple-darwin24.6.0 \
  --id sip:1000@127.0.0.1 \
  --registrar sip:127.0.0.1 \
  --realm "*" \
  --username 1000 \
  --password 1234 \
  --null-audio

# 在 pjsua 提示符下输入
> call sip:1001@127.0.0.1
```

## 故障排查

### 容器无法启动

1. 检查 Docker 服务是否运行：
   ```bash
   docker info
   ```

2. 查看容器日志：
   ```bash
   docker compose logs
   ```

3. 检查端口占用：
   ```bash
   lsof -i :5060
   ```

### SIP 注册失败

1. 确认 FreeSWITCH 服务正在运行：
   ```bash
   docker exec freeswitch-local fs_cli -x "status"
   ```

2. 检查 SIP profiles：
   ```bash
   docker exec freeswitch-local fs_cli -x "sofia status"
   ```

3. 查看日志中的错误信息：
   ```bash
   docker compose logs -f | grep -i error
   ```

### 重置环境

如果遇到配置问题，可以完全重置：

```bash
# 1. 卸载现有部署
./uninstall.sh

# 2. 清理所有相关资源
docker system prune -a

# 3. 重新安装
./install.sh
./start.sh
```

## 技术细节

### Docker 镜像

- **镜像**: safarov/freeswitch:latest
- **FreeSWITCH 版本**: 1.10.x

### Docker Volumes

- `freeswitch-sounds` - 音频文件
- `freeswitch-recordings` - 录音文件
- `freeswitch-logs` - 日志文件

### 配置挂载

- `./config/directory` -> `/etc/freeswitch/directory` (只读)

## 注意事项

1. **本地部署**: 所有服务绑定到 127.0.0.1，仅本机可访问
2. **测试用途**: 默认密码为弱密码 (1234)，不建议生产使用
3. **端口限制**: RTP 端口范围较小 (16384-16394)，支持约 5 个并发通话
4. **性能**: 容器资源受 Docker 限制，不适合高负载场景

## 相关文档

- [FreeSWITCH 官方文档](https://freeswitch.org/confluence/)
- [Docker Compose 文档](https://docs.docker.com/compose/)
- [PJSIP 文档](https://docs.pjsip.org/)

## 许可证

本脚本遵循项目根目录的许可证。FreeSWITCH 遵循 MPL 1.1 许可证。
