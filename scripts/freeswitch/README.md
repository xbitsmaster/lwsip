# FreeSWITCH Docker 部署指南

## 概述

本目录包含在服务器 `198.19.249.149` 上使用Docker部署FreeSWITCH的配置文件和脚本。

## 前提条件

### 本地机器
- SSH客户端
- 已配置对目标服务器的SSH密钥访问

### 目标服务器 (198.19.249.149)
- Linux系统（推荐Ubuntu 20.04+或CentOS 7+）
- root或sudo权限
- Docker和docker-compose（脚本会自动安装）
- 开放端口：
  - 5060 (SIP TCP/UDP)
  - 5080 (SIP TCP/UDP，备用)
  - 16384-16394 (RTP UDP)
  - 8021 (ESL管理端口，可选)

## 快速部署

### 方法1：自动部署脚本

```bash
cd /Users/konghan/ClaudeSpace/lwsip/freeswitch_deploy
./deploy.sh
```

**注意**：部署前请修改 `deploy.sh` 中的以下变量：
- `REMOTE_USER`: SSH用户名（默认为root）
- 确认SSH密钥已配置

### 方法2：手动部署

1. **上传文件到服务器**
```bash
scp -r freeswitch_deploy root@198.19.249.149:/opt/
```

2. **登录服务器**
```bash
ssh root@198.19.249.149
```

3. **安装Docker（如果未安装）**
```bash
curl -fsSL https://get.docker.com | sh
systemctl start docker
systemctl enable docker
```

4. **安装docker-compose**
```bash
curl -L "https://github.com/docker/compose/releases/latest/download/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
chmod +x /usr/local/bin/docker-compose
```

5. **启动FreeSWITCH**
```bash
cd /opt/freeswitch_deploy
docker-compose up -d
```

6. **查看日志**
```bash
docker-compose logs -f freeswitch
```

## 测试用户

部署后，FreeSWITCH会预配置以下测试用户：

| 用户名 | 密码 | 用途 |
|--------|------|------|
| 1000 | 1234 | Lwsip测试 |
| 1001 | 1234 | PJSUA测试 |
| 1002 | 1234 | 备用测试 |

## 使用方法

### 注册到FreeSWITCH

**PJSUA示例**：
```bash
./pjsua-aarch64-apple-darwin24.6.0 \
  --id sip:1001@198.19.249.149 \
  --registrar sip:198.19.249.149 \
  --realm "*" \
  --username 1001 \
  --password 1234
```

**Lwsip配置**：
```c
// 在lwsip中配置
server: 198.19.249.149:5060
username: 1000
password: 1234
```

### 测试呼叫

从PJSUA呼叫Lwsip：
```
# 在PJSUA控制台
m             # 进入呼叫菜单
sip:1000@198.19.249.149  # 呼叫1000
```

## 管理命令

### 查看容器状态
```bash
ssh root@198.19.249.149 'cd /opt/freeswitch && docker-compose ps'
```

### 查看实时日志
```bash
ssh root@198.19.249.149 'cd /opt/freeswitch && docker-compose logs -f'
```

### 重启FreeSWITCH
```bash
ssh root@198.19.249.149 'cd /opt/freeswitch && docker-compose restart'
```

### 停止FreeSWITCH
```bash
ssh root@198.19.249.149 'cd /opt/freeswitch && docker-compose down'
```

### 进入FreeSWITCH控制台
```bash
ssh root@198.19.249.149
docker exec -it freeswitch fs_cli
```

FreeSWITCH CLI常用命令：
```
sofia status                    # 查看SIP状态
sofia status profile internal   # 查看internal profile
show registrations              # 查看已注册用户
show channels                   # 查看当前通话
show calls                      # 查看呼叫列表
```

## 故障排查

### 无法连接SIP服务器

1. **检查防火墙**
```bash
# 在服务器上
firewall-cmd --list-ports  # CentOS
ufw status                  # Ubuntu

# 开放端口（如果需要）
firewall-cmd --add-port=5060/tcp --permanent
firewall-cmd --add-port=5060/udp --permanent
firewall-cmd --add-port=16384-16394/udp --permanent
firewall-cmd --reload
```

2. **检查容器状态**
```bash
docker-compose ps
docker-compose logs freeswitch | tail -100
```

3. **检查端口监听**
```bash
netstat -tunlp | grep 5060
```

### 注册失败

1. 检查用户名和密码是否正确
2. 查看FreeSWITCH日志：`docker-compose logs -f freeswitch`
3. 确认realm设置为 `*` 或正确的域名

### 无音频

1. 检查RTP端口是否开放（16384-16394）
2. 确认防火墙规则
3. 检查NAT配置

## 网络架构

```
[本地Lwsip/PJSUA]
       |
       | SIP (5060)
       | RTP (16384-16394)
       v
[198.19.249.149:FreeSWITCH Docker]
```

## 配置文件说明

- `docker-compose.yml`: Docker编排配置
- `config/directory/*.xml`: SIP用户配置
- `config/sip_profiles/`: SIP配置文件（使用默认配置）
- `config/vars.xml`: 全局变量配置（可选）

## 安全建议

1. 修改默认密码（在`config/directory/*.xml`中）
2. 限制SSH访问IP
3. 配置防火墙规则
4. 启用TLS/SRTP（生产环境）
5. 定期更新FreeSWITCH镜像

## 参考资料

- [FreeSWITCH官方文档](https://freeswitch.org/confluence/)
- [Docker Hub - SignalWire FreeSWITCH](https://hub.docker.com/r/signalwire/freeswitch)
