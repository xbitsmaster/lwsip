#!/bin/bash

# FreeSWITCH 本地 Docker 启动脚本
# 用途：启动 FreeSWITCH Docker 容器

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================="
echo "FreeSWITCH Docker 启动脚本"
echo "========================================="
echo ""

# 检查 Docker
check_docker() {
    if ! command -v docker &> /dev/null; then
        echo "错误: Docker 未安装"
        echo "请先运行: ./install.sh"
        exit 1
    fi

    if ! docker info &> /dev/null; then
        echo "错误: Docker 服务未运行"
        echo "请启动 Docker 服务"
        exit 1
    fi
}

# 检查配置文件
check_config() {
    if [ ! -f "docker-compose.yml" ]; then
        echo "错误: docker-compose.yml 不存在"
        echo "请先运行: ./install.sh"
        exit 1
    fi
}

# 检查容器状态
check_container_status() {
    if docker ps -a | grep -q "freeswitch-local"; then
        if docker ps | grep -q "freeswitch-local"; then
            echo "注意: FreeSWITCH 容器已在运行"
            echo ""
            read -p "是否重启容器? (y/N): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                echo "重启容器..."
                docker compose restart
                echo "✓ 容器已重启"
                return
            else
                echo "保持当前运行状态"
                show_status
                exit 0
            fi
        fi
    fi
}

# 启动容器
start_container() {
    echo "启动 FreeSWITCH 容器..."
    docker compose up -d

    echo ""
    echo "等待 FreeSWITCH 启动..."
    sleep 5

    # 检查容器是否正常运行
    if docker ps | grep -q "freeswitch-local"; then
        echo "✓ FreeSWITCH 容器启动成功"
    else
        echo "✗ FreeSWITCH 容器启动失败"
        echo ""
        echo "查看日志:"
        docker compose logs --tail=50
        exit 1
    fi
    echo ""
}

# 等待服务就绪
wait_for_service() {
    echo "等待 FreeSWITCH 服务就绪..."
    local max_wait=30
    local wait_time=0

    while [ $wait_time -lt $max_wait ]; do
        if docker exec freeswitch-local fs_cli -x "status" &> /dev/null; then
            echo "✓ FreeSWITCH 服务已就绪"
            return 0
        fi
        sleep 2
        wait_time=$((wait_time + 2))
        echo "  等待中... ($wait_time/$max_wait 秒)"
    done

    echo "⚠ FreeSWITCH 服务启动超时"
    echo "  容器正在运行，但服务可能还未完全启动"
    echo "  请稍后使用以下命令检查状态:"
    echo "    docker exec freeswitch-local fs_cli -x 'status'"
    return 1
}

# 显示服务状态
show_status() {
    echo "========================================="
    echo "FreeSWITCH 服务状态"
    echo "========================================="
    echo ""

    # 容器状态
    echo "容器状态:"
    docker ps -a | grep freeswitch-local || echo "  未找到容器"
    echo ""

    # 端口监听
    echo "端口监听:"
    docker port freeswitch-local 2>/dev/null || echo "  无法获取端口信息"
    echo ""

    # FreeSWITCH 状态
    echo "FreeSWITCH 服务:"
    if docker exec freeswitch-local fs_cli -x "status" 2>/dev/null; then
        echo "  ✓ 服务正常"
    else
        echo "  ✗ 服务未就绪"
    fi
    echo ""

    # SIP profiles
    echo "SIP Profiles:"
    docker exec freeswitch-local fs_cli -x "sofia status" 2>/dev/null || echo "  无法获取 SIP 状态"
    echo ""
}

# 显示使用信息
show_usage_info() {
    echo "========================================="
    echo "启动完成！"
    echo "========================================="
    echo ""
    echo "FreeSWITCH 服务信息:"
    echo "  - 服务器地址: 127.0.0.1"
    echo "  - SIP 端口: 5060 (TCP/UDP)"
    echo "  - RTP 端口: 16384-16394 (UDP)"
    echo ""
    echo "测试账号 (密码均为 1234):"
    echo "  - 1000@127.0.0.1"
    echo "  - 1001@127.0.0.1"
    echo "  - 1002@127.0.0.1"
    echo "  - 1003@127.0.0.1"
    echo ""
    echo "常用命令:"
    echo "  查看日志:   docker compose logs -f"
    echo "  进入控制台: docker exec -it freeswitch-local fs_cli"
    echo "  查看状态:   docker exec freeswitch-local fs_cli -x 'status'"
    echo "  查看注册:   docker exec freeswitch-local fs_cli -x 'show registrations'"
    echo "  停止服务:   ./stop.sh"
    echo ""
}

# 主流程
main() {
    check_docker
    check_config
    check_container_status
    start_container
    wait_for_service
    show_status
    show_usage_info
}

main
