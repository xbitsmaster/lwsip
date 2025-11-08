#!/bin/bash

# FreeSWITCH 远程控制脚本
# 用途：管理远程服务器上的 FreeSWITCH 服务

set -e

REMOTE_HOST="198.19.249.149"
REMOTE_USER="root"
REMOTE_DIR="/opt/freeswitch"

usage() {
    cat <<EOF
用法: $0 <命令>

命令:
  start       启动 FreeSWITCH 服务
  stop        停止 FreeSWITCH 服务
  restart     重启 FreeSWITCH 服务
  status      查看服务状态
  logs        查看日志（实时）
  check       检查注册和通话
  shell       进入 FreeSWITCH 控制台

示例:
  $0 start        # 启动服务
  $0 status       # 查看状态
  $0 logs         # 查看日志

EOF
    exit 0
}

if [ $# -lt 1 ]; then
    usage
fi

COMMAND=$1

case "$COMMAND" in
    start)
        echo "启动 FreeSWITCH 服务..."
        ssh ${REMOTE_USER}@${REMOTE_HOST} "cd ${REMOTE_DIR} && docker compose up -d"
        echo "✓ 服务已启动"
        echo ""
        echo "等待 10 秒让服务完全启动..."
        sleep 10
        $0 status
        ;;

    stop)
        echo "停止 FreeSWITCH 服务..."
        ssh ${REMOTE_USER}@${REMOTE_HOST} "cd ${REMOTE_DIR} && docker compose down"
        echo "✓ 服务已停止"
        ;;

    restart)
        echo "重启 FreeSWITCH 服务..."
        ssh ${REMOTE_USER}@${REMOTE_HOST} "cd ${REMOTE_DIR} && docker compose restart"
        echo "✓ 服务已重启"
        echo ""
        echo "等待 10 秒让服务完全启动..."
        sleep 10
        $0 status
        ;;

    status)
        echo "========================================="
        echo "FreeSWITCH 服务状态"
        echo "========================================="
        echo ""
        echo "容器状态:"
        ssh ${REMOTE_USER}@${REMOTE_HOST} "docker ps -a | grep freeswitch || echo '  未找到容器'"
        echo ""
        echo "端口监听:"
        ssh ${REMOTE_USER}@${REMOTE_HOST} "netstat -tuln 2>/dev/null | grep 5060 || ss -tuln 2>/dev/null | grep 5060 || echo '  端口 5060 未监听'"
        echo ""
        echo "FreeSWITCH 服务状态:"
        ssh ${REMOTE_USER}@${REMOTE_HOST} "docker exec freeswitch fs_cli -x 'status' 2>/dev/null || echo '  服务未就绪'"
        ;;

    logs)
        echo "查看 FreeSWITCH 日志 (Ctrl+C 退出)..."
        ssh ${REMOTE_USER}@${REMOTE_HOST} "cd ${REMOTE_DIR} && docker compose logs -f"
        ;;

    check)
        echo "========================================="
        echo "FreeSWITCH 状态检查"
        echo "========================================="
        echo ""
        echo "SIP Profiles:"
        ssh ${REMOTE_USER}@${REMOTE_HOST} "docker exec freeswitch fs_cli -x 'sofia status' 2>/dev/null || echo '  无法获取状态'"
        echo ""
        echo "注册用户:"
        ssh ${REMOTE_USER}@${REMOTE_HOST} "docker exec freeswitch fs_cli -x 'show registrations' 2>/dev/null || echo '  无法获取注册信息'"
        echo ""
        echo "当前通话:"
        ssh ${REMOTE_USER}@${REMOTE_HOST} "docker exec freeswitch fs_cli -x 'show channels' 2>/dev/null || echo '  无法获取通话信息'"
        ;;

    shell)
        echo "进入 FreeSWITCH 控制台 (输入 /exit 退出)..."
        ssh -t ${REMOTE_USER}@${REMOTE_HOST} "docker exec -it freeswitch fs_cli"
        ;;

    *)
        echo "错误: 未知命令 '$COMMAND'"
        usage
        ;;
esac
