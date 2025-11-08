#!/bin/bash

# FreeSWITCH 本地 Docker 停止脚本
# 用途：停止 FreeSWITCH Docker 容器

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================="
echo "FreeSWITCH Docker 停止脚本"
echo "========================================="
echo ""

# 检查 Docker
check_docker() {
    if ! command -v docker &> /dev/null; then
        echo "错误: Docker 未安装"
        exit 1
    fi

    if ! docker info &> /dev/null; then
        echo "错误: Docker 服务未运行"
        exit 1
    fi
}

# 检查容器状态
check_container() {
    if ! docker ps | grep -q "freeswitch-local"; then
        echo "注意: FreeSWITCH 容器未运行"
        echo ""

        if docker ps -a | grep -q "freeswitch-local"; then
            echo "容器已存在但未运行"
        else
            echo "容器不存在"
        fi
        exit 0
    fi
}

# 显示当前运行信息
show_running_info() {
    echo "当前运行的 FreeSWITCH 容器:"
    docker ps | grep freeswitch-local
    echo ""

    # 显示当前连接
    echo "当前 SIP 注册:"
    docker exec freeswitch-local fs_cli -x "show registrations" 2>/dev/null || echo "  无法获取注册信息"
    echo ""

    echo "当前通话:"
    docker exec freeswitch-local fs_cli -x "show channels" 2>/dev/null || echo "  无法获取通话信息"
    echo ""
}

# 停止容器
stop_container() {
    local force_stop=false

    # 检查是否有活动通话
    local active_calls
    active_calls=$(docker exec freeswitch-local fs_cli -x "show channels count" 2>/dev/null | grep "total" | awk '{print $1}' || echo "0")

    if [ "$active_calls" != "0" ] && [ "$active_calls" != "" ]; then
        echo "⚠ 警告: 当前有 $active_calls 个活动通话"
        echo ""
        read -p "是否强制停止? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            echo "取消停止操作"
            exit 0
        fi
        force_stop=true
    fi

    echo "停止 FreeSWITCH 容器..."

    if [ "$force_stop" = true ]; then
        # 强制停止
        docker compose stop -t 5
    else
        # 优雅停止
        docker compose stop
    fi

    echo "✓ 容器已停止"
    echo ""
}

# 显示容器状态
show_status() {
    echo "容器状态:"
    docker ps -a | grep freeswitch-local
    echo ""
}

# 显示后续操作
show_next_steps() {
    echo "========================================="
    echo "停止完成！"
    echo "========================================="
    echo ""
    echo "后续操作:"
    echo "  启动服务:   ./start.sh"
    echo "  卸载服务:   ./uninstall.sh"
    echo "  查看日志:   docker compose logs"
    echo ""
    echo "注意: 容器已停止但未删除"
    echo "      数据卷和配置已保留"
    echo ""
}

# 主流程
main() {
    check_docker
    check_container
    show_running_info
    stop_container
    show_status
    show_next_steps
}

main
