#!/bin/bash

# FreeSWITCH 远程部署脚本
# 用途：将 FreeSWITCH 部署到远程服务器 198.19.249.149

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 远程服务器配置
REMOTE_HOST="198.19.249.149"
REMOTE_USER="root"
REMOTE_DIR="/opt/freeswitch"

echo "========================================="
echo "FreeSWITCH 远程部署脚本"
echo "========================================="
echo "目标服务器: ${REMOTE_USER}@${REMOTE_HOST}"
echo "部署目录: ${REMOTE_DIR}"
echo ""

# 检查 SSH 连接
echo "检查远程服务器连接..."
if ! ssh -o ConnectTimeout=5 ${REMOTE_USER}@${REMOTE_HOST} "echo 'SSH 连接成功'"; then
    echo "错误: 无法连接到远程服务器"
    exit 1
fi
echo ""

# 检查本地配置文件
echo "检查本地配置文件..."
if [ ! -f "docker-compose.yml" ]; then
    echo "错误: docker-compose.yml 不存在"
    exit 1
fi

if [ ! -d "config/directory" ]; then
    echo "错误: config/directory 目录不存在"
    exit 1
fi

echo "✓ 本地配置文件检查通过"
echo ""

# 在远程服务器上创建目录
echo "在远程服务器上创建目录..."
ssh ${REMOTE_USER}@${REMOTE_HOST} "mkdir -p ${REMOTE_DIR}/config/directory"
echo "✓ 目录创建完成"
echo ""

# 传输配置文件
echo "传输配置文件到远程服务器..."
scp -r docker-compose.yml ${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_DIR}/
scp -r config/directory/*.xml ${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_DIR}/config/directory/
echo "✓ 配置文件传输完成"
echo ""

# 在远程服务器上检查并安装 Docker
echo "检查远程服务器 Docker 环境..."
ssh ${REMOTE_USER}@${REMOTE_HOST} bash <<'ENDSSH'
    set -e

    # 检查 Docker
    if ! command -v docker &> /dev/null; then
        echo "Docker 未安装，正在安装..."
        curl -fsSL https://get.docker.com -o get-docker.sh
        sh get-docker.sh
        rm get-docker.sh
        systemctl enable docker
        systemctl start docker
        echo "✓ Docker 安装完成"
    else
        echo "✓ Docker 已安装"
        docker --version
    fi

    # 检查 Docker Compose
    if docker compose version &> /dev/null; then
        echo "✓ Docker Compose (plugin) 可用"
        docker compose version
    elif command -v docker-compose &> /dev/null; then
        echo "✓ Docker Compose (standalone) 可用"
        docker-compose --version
    else
        echo "错误: Docker Compose 未安装"
        exit 1
    fi
ENDSSH

echo ""

# 拉取 Docker 镜像
echo "拉取 FreeSWITCH Docker 镜像..."
ssh ${REMOTE_USER}@${REMOTE_HOST} "cd ${REMOTE_DIR} && docker pull safarov/freeswitch:latest"
echo "✓ 镜像拉取完成"
echo ""

# 停止旧容器（如果存在）
echo "检查并停止旧容器..."
ssh ${REMOTE_USER}@${REMOTE_HOST} "cd ${REMOTE_DIR} && docker compose down 2>/dev/null || true"
echo ""

# 启动 FreeSWITCH
echo "启动 FreeSWITCH 容器..."
ssh ${REMOTE_USER}@${REMOTE_HOST} "cd ${REMOTE_DIR} && docker compose up -d"
echo "✓ FreeSWITCH 容器已启动"
echo ""

# 等待服务启动
echo "等待 FreeSWITCH 服务启动..."
sleep 10

# 检查容器状态
echo "检查容器状态..."
ssh ${REMOTE_USER}@${REMOTE_HOST} "cd ${REMOTE_DIR} && docker ps | grep freeswitch"
echo ""

# 检查端口监听
echo "检查端口监听..."
ssh ${REMOTE_USER}@${REMOTE_HOST} "netstat -tuln | grep 5060 || ss -tuln | grep 5060"
echo ""

echo "========================================="
echo "部署完成！"
echo "========================================="
echo ""
echo "FreeSWITCH 服务信息:"
echo "  - 服务器地址: ${REMOTE_HOST}"
echo "  - SIP 端口: 5060 (TCP/UDP)"
echo "  - RTP 端口: 16384-16394 (UDP)"
echo ""
echo "SIP 用户账号 (密码均为 1234):"
echo "  - 1000@${REMOTE_HOST}"
echo "  - 1001@${REMOTE_HOST}"
echo "  - 1002@${REMOTE_HOST}"
echo "  - 1003@${REMOTE_HOST}"
echo ""
echo "管理命令:"
echo "  查看日志: ssh ${REMOTE_USER}@${REMOTE_HOST} 'cd ${REMOTE_DIR} && docker compose logs -f'"
echo "  查看状态: ssh ${REMOTE_USER}@${REMOTE_HOST} 'cd ${REMOTE_DIR} && docker exec freeswitch fs_cli -x status'"
echo "  停止服务: ssh ${REMOTE_USER}@${REMOTE_HOST} 'cd ${REMOTE_DIR} && docker compose down'"
echo "  重启服务: ssh ${REMOTE_USER}@${REMOTE_HOST} 'cd ${REMOTE_DIR} && docker compose restart'"
echo ""
