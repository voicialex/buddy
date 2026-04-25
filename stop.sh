#!/usr/bin/env bash
# stop.sh — 停止所有 buddy_robot 节点
set -euo pipefail

NODES=(audio_node vision_node cloud_node state_machine_node dialog_node sentence_node)

for node in "${NODES[@]}"; do
    pkill -f "$node" 2>/dev/null && echo "  ✓ $node" || echo "  - $node (未运行)"
done
echo "[OK] 已停止"
