#!/bin/bash

# FreeSWITCH Docker 部署脚本
# 用于在远程服务器198.19.249.149上部署FreeSWITCH

set -e

REMOTE_HOST="198.19.249.149"
REMOTE_USER="root"  # 根据实际情况修改
DEPLOY_DIR="/opt/freeswitch"

echo "========================================="
echo "FreeSWITCH Docker 部署脚本"
echo "目标服务器: ${REMOTE_HOST}"
echo "========================================="

# 检查SSH连接
echo "检查SSH连接..."
if ! ssh -o ConnectTimeout=5 ${REMOTE_USER}@${REMOTE_HOST} "echo 'SSH连接成功'"; then
    echo "错误: 无法连接到 ${REMOTE_HOST}"
    echo "请确保："
    echo "  1. SSH密钥已配置"
    echo "  2. 服务器地址正确"
    echo "  3. 网络连接正常"
    exit 1
fi

# 在远程服务器上创建部署目录
echo "创建远程部署目录..."
ssh ${REMOTE_USER}@${REMOTE_HOST} "mkdir -p ${DEPLOY_DIR}"

# 复制配置文件
echo "上传配置文件..."
scp -r docker-compose.yml config ${REMOTE_USER}@${REMOTE_HOST}:${DEPLOY_DIR}/

# 在远程服务器上部署
echo "启动FreeSWITCH容器..."
ssh ${REMOTE_USER}@${REMOTE_HOST} << 'EOF'
cd /opt/freeswitch

# 检查Docker是否安装
if ! command -v docker &> /dev/null; then
    echo "Docker未安装，正在安装..."
    curl -fsSL https://get.docker.com | sh
    systemctl start docker
    systemctl enable docker
fi

# 检查docker-compose是否安装
if ! command -v docker-compose &> /dev/null; then
    echo "docker-compose未安装，正在安装..."
    curl -L "https://github.com/docker/compose/releases/latest/download/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
    chmod +x /usr/local/bin/docker-compose
fi

# 停止旧容器（如果存在）
docker-compose down 2>/dev/null || true

# 启动FreeSWITCH
docker-compose up -d

# 等待容器启动
echo "等待FreeSWITCH启动..."
sleep 10

# 检查状态
docker-compose ps
docker-compose logs --tail=50 freeswitch

echo "========================================="
echo "FreeSWITCH 部署完成！"
echo "SIP端口: 5060"
echo "RTP端口: 16384-16394"
echo "========================================="
echo ""
echo "测试用户:"
echo "  用户1: 1000 / 密码: 1234"
echo "  用户2: 1001 / 密码: 1234"
echo "  用户3: 1002 / 密码: 1234"
echo "========================================="
EOF

echo ""
echo "部署完成！您可以使用以下命令查看日志："
echo "  ssh ${REMOTE_USER}@${REMOTE_HOST} 'cd ${DEPLOY_DIR} && docker-compose logs -f'"
