#!/bin/bash

# FreeSWITCH 本地 Docker 部署安装脚本
# 用途：安装并配置基于 Docker 的 FreeSWITCH 服务

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================="
echo "FreeSWITCH Docker 安装脚本"
echo "========================================="
echo ""

# 检查 Docker 是否安装
check_docker() {
    echo "检查 Docker 环境..."
    if ! command -v docker &> /dev/null; then
        echo "错误: Docker 未安装"
        echo "请先安装 Docker: https://docs.docker.com/get-docker/"
        exit 1
    fi

    if ! docker info &> /dev/null; then
        echo "错误: Docker 服务未运行"
        echo "请启动 Docker 服务"
        exit 1
    fi

    echo "✓ Docker 环境检查通过"
    docker --version
    echo ""
}

# 检查 Docker Compose
check_docker_compose() {
    echo "检查 Docker Compose..."
    if docker compose version &> /dev/null; then
        echo "✓ Docker Compose (plugin) 可用"
        docker compose version
    elif command -v docker-compose &> /dev/null; then
        echo "✓ Docker Compose (standalone) 可用"
        docker-compose --version
    else
        echo "错误: Docker Compose 未安装"
        echo "请安装 Docker Compose: https://docs.docker.com/compose/install/"
        exit 1
    fi
    echo ""
}

# 检查配置文件
check_config() {
    echo "检查配置文件..."

    if [ ! -f "docker-compose.yml" ]; then
        echo "错误: docker-compose.yml 不存在"
        exit 1
    fi

    if [ ! -d "config/directory" ]; then
        echo "错误: config/directory 目录不存在"
        exit 1
    fi

    # 检查用户配置
    for user in 1000 1001 1002 1003; do
        if [ ! -f "config/directory/${user}.xml" ]; then
            echo "错误: config/directory/${user}.xml 不存在"
            exit 1
        fi
    done

    echo "✓ 配置文件检查通过"
    echo "  - docker-compose.yml"
    echo "  - config/directory/1000.xml (密码: 1234)"
    echo "  - config/directory/1001.xml (密码: 1234)"
    echo "  - config/directory/1002.xml (密码: 1234)"
    echo "  - config/directory/1003.xml (密码: 1234)"
    echo ""
}

# 拉取 Docker 镜像
pull_image() {
    echo "拉取 FreeSWITCH Docker 镜像..."
    docker pull safarov/freeswitch:latest
    echo "✓ 镜像拉取完成"
    echo ""
}

# 显示安装信息
show_info() {
    echo "========================================="
    echo "安装完成！"
    echo "========================================="
    echo ""
    echo "FreeSWITCH 服务配置:"
    echo "  - 服务器地址: 127.0.0.1"
    echo "  - SIP 端口: 5060 (TCP/UDP)"
    echo "  - RTP 端口: 16384-16394 (UDP)"
    echo "  - ESL 端口: 8021 (TCP)"
    echo ""
    echo "SIP 用户账号 (密码均为 1234):"
    echo "  - 1000@127.0.0.1"
    echo "  - 1001@127.0.0.1"
    echo "  - 1002@127.0.0.1"
    echo "  - 1003@127.0.0.1"
    echo ""
    echo "下一步操作:"
    echo "  启动服务: ./start.sh"
    echo "  停止服务: ./stop.sh"
    echo "  卸载服务: ./uninstall.sh"
    echo ""
}

# 主流程
main() {
    check_docker
    check_docker_compose
    check_config
    pull_image
    show_info
}

main
