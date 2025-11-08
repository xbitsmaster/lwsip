#!/bin/bash

# FreeSWITCH 本地 Docker 部署卸载脚本
# 用途：停止并删除 FreeSWITCH Docker 容器及相关资源

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================="
echo "FreeSWITCH Docker 卸载脚本"
echo "========================================="
echo ""

# 检查 Docker
check_docker() {
    if ! command -v docker &> /dev/null; then
        echo "错误: Docker 未安装"
        exit 1
    fi
}

# 确认卸载
confirm_uninstall() {
    echo "警告: 此操作将删除以下内容："
    echo "  - FreeSWITCH Docker 容器"
    echo "  - Docker volumes (日志、录音、声音文件)"
    echo "  - Docker network"
    echo ""
    read -p "确认要卸载吗? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "取消卸载"
        exit 0
    fi
}

# 停止并删除容器
remove_containers() {
    echo "停止并删除容器..."

    if docker compose ps -q freeswitch &> /dev/null; then
        docker compose down
        echo "✓ 容器已停止并删除"
    else
        echo "✓ 没有运行的容器"
    fi
    echo ""
}

# 删除 volumes
remove_volumes() {
    echo "删除 Docker volumes..."

    local volumes=(
        "freeswitch_freeswitch-sounds"
        "freeswitch_freeswitch-recordings"
        "freeswitch_freeswitch-logs"
    )

    for vol in "${volumes[@]}"; do
        if docker volume ls -q | grep -q "^${vol}$"; then
            docker volume rm "$vol" 2>/dev/null && echo "  ✓ 已删除: $vol" || echo "  ✗ 删除失败: $vol"
        fi
    done
    echo ""
}

# 删除 network
remove_networks() {
    echo "删除 Docker networks..."

    local network="freeswitch_freeswitch_net"
    if docker network ls -q -f name="^${network}$" | grep -q .; then
        docker network rm "$network" 2>/dev/null && echo "  ✓ 已删除: $network" || echo "  ✗ 删除失败: $network"
    else
        echo "  ✓ 没有需要删除的 network"
    fi
    echo ""
}

# 删除镜像（可选）
remove_image_prompt() {
    echo "FreeSWITCH Docker 镜像信息:"
    docker images safarov/freeswitch
    echo ""

    read -p "是否删除 FreeSWITCH 镜像? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "删除镜像..."
        docker rmi safarov/freeswitch:latest 2>/dev/null && echo "✓ 镜像已删除" || echo "✗ 镜像删除失败"
    else
        echo "保留镜像"
    fi
    echo ""
}

# 显示卸载结果
show_result() {
    echo "========================================="
    echo "卸载完成！"
    echo "========================================="
    echo ""
    echo "配置文件保留在当前目录："
    echo "  - docker-compose.yml"
    echo "  - config/"
    echo ""
    echo "如需重新安装，运行: ./install.sh"
    echo ""
}

# 主流程
main() {
    check_docker
    confirm_uninstall
    remove_containers
    remove_volumes
    remove_networks
    remove_image_prompt
    show_result
}

main
