#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export LD_LIBRARY_PATH="$DIR/lib/sherpa:$DIR/lib:${LD_LIBRARY_PATH:-}"

# Ensure ROS 2 package prefix environment exists for class_loader/ament_index.
# Prefer full workspace setup when packaged install tree is present; otherwise
# fall back to a minimal prefix so class_loader won't fail on empty AMENT_PREFIX_PATH.
if [[ -f "$DIR/setup.bash" ]]; then
    set +u
    # shellcheck disable=SC1090
    source "$DIR/setup.bash"
    set -u
else
    export AMENT_PREFIX_PATH="${DIR}:${AMENT_PREFIX_PATH:-}"
    export COLCON_PREFIX_PATH="${DIR}:${COLCON_PREFIX_PATH:-}"
    export CMAKE_PREFIX_PATH="${DIR}:${CMAKE_PREFIX_PATH:-}"
fi

if [[ -f "$DIR/etc/buddy.env" ]]; then
    set -a; source "$DIR/etc/buddy.env"; set +a
fi

if [[ -x "$DIR/scripts/status.sh" ]]; then
    "$DIR/scripts/status.sh" --module || true
fi

# ── Audio hardware init ────────────────────────────────────────────
_init_audio() {
    local cdev card
    for card in /dev/snd/controlC*; do
        [ -e "$card" ] || continue
        cdev="${card##*controlC}"
        local info
        info="$(amixer -c "$cdev" info 2>/dev/null || true)"

        # RT5616 onboard codec (built-in mic/speaker)
        if echo "$info" | grep -qi "rt5616"; then
            amixer -c "$cdev" cset numid=23 on,on >/dev/null 2>&1 || true
            amixer -c "$cdev" set "IN1 Boost" 1 >/dev/null 2>&1 || true
            amixer -c "$cdev" set "ADC Boost" 1 >/dev/null 2>&1 || true
            echo "[INFO] Audio init: RT5616 card=$cdev capture=on IN1=1 ADC=1"
            continue
        fi

        # USB sound card — max out PCM + Mic, disable auto-gain
        if echo "$info" | grep -qi "USB"; then
            amixer -c "$cdev" set "PCM" 100% on >/dev/null 2>&1 || true
            amixer -c "$cdev" set "Mic" 100% on >/dev/null 2>&1 || true
            amixer -c "$cdev" set "Auto Gain Control" off >/dev/null 2>&1 || true
            echo "[INFO] Audio init: USB card=$cdev PCM=max Mic=max AGC=off"
            continue
        fi
    done
}
_init_audio

# Redirect ROS 2 logs to /tmp/buddy
export ROS_HOME=/tmp/buddy

echo "[INFO] Launching buddy_main..."
cd "$DIR"
exec "$DIR/bin/buddy_main" --base-dir "$DIR"
