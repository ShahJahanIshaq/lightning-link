#!/usr/bin/env bash
# Lightning Link loss-tolerance sweep. Holds latency fixed at 100 ms and sweeps
# packet loss across {0, 1, 3, 5, 10}% for both transports. Designed to expose:
#   * how gracefully UDP + prediction + interpolation degrade when packets drop;
#   * the tail explosion of TCP retransmission when the loss rate rises;
#   * snapshot-arrival CV vs loss, motivating entity interpolation.
#
# Output layout:
#   experiments_loss/loss<pct>_<mode>_rep<N>/{server,client}_*.csv
# Consumed by: tools/analyze.py --mode loss

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${REPO_DIR}/build"
SERVER="${BUILD_DIR}/lightning_link_server"
CLIENT="${BUILD_DIR}/lightning_link_client"
EXP_DIR="${REPO_DIR}/experiments_loss"

DURATION_SEC="${DURATION_SEC:-30}"
REPS="${REPS:-3}"
BASE_UDP_PORT="${BASE_UDP_PORT:-56421}"
BASE_TCP_PORT="${BASE_TCP_PORT:-56422}"
LATENCY_MS="${LATENCY_MS:-100}"
CLIENT_COUNT="${CLIENT_COUNT:-2}"

mkdir -p "$EXP_DIR"

if [[ ! -x "$SERVER" || ! -x "$CLIENT" ]]; then
    echo "error: server/client binaries not found. Run 'cmake --build build -j' first." >&2
    exit 1
fi

LOSS_LEVELS=(0 1 3 5 10)
MODES=(optimized baseline)

run_one() {
    local loss="$1" mode="$2" rep="$3"
    local seed=$((3000 * rep + 17))
    local label="loss${loss}_${mode}_rep${rep}"
    local out_dir="${EXP_DIR}/${label}"
    rm -rf "$out_dir"
    mkdir -p "$out_dir"

    local udp_port=$((BASE_UDP_PORT + rep))
    local tcp_port=$((BASE_TCP_PORT + rep))
    local srv_dur=$((DURATION_SEC + 2))

    echo ">> loss sweep: mode=${mode} loss=${loss}% rep=${rep}"

    "$SERVER" --mode="$mode" --duration="$srv_dur" \
        --port="$udp_port" --tcp-port="$tcp_port" \
        --run-label="$label" --log-dir="$out_dir" \
        --seed="$seed" \
        >"$out_dir/server.stdout" 2>&1 &
    local srv_pid=$!

    sleep 0.5

    local pids=()
    for ((i = 0; i < CLIENT_COUNT; ++i)); do
        local bot_pattern
        case $((i % 4)) in
            0) bot_pattern=circle ;;
            1) bot_pattern=sine   ;;
            2) bot_pattern=zigzag ;;
            *) bot_pattern=random ;;
        esac
        "$CLIENT" --mode="$mode" --server=127.0.0.1 \
            --port="$udp_port" --tcp-port="$tcp_port" \
            --duration="$DURATION_SEC" \
            --run-label="$label" --log-dir="$out_dir" \
            --bot="$bot_pattern" --bot-seed=$((seed + i)) --headless \
            --add-latency="$LATENCY_MS" --loss="$loss" --seed=$((seed + i + 1)) \
            >"$out_dir/client_${i}.stdout" 2>&1 &
        pids+=($!)
        sleep 0.05
    done

    for pid in "${pids[@]}"; do wait "$pid" || true; done
    wait "$srv_pid" || true

    echo "   done: $(ls "$out_dir"/*.csv 2>/dev/null | wc -l | tr -d ' ') csv files"
}

for loss in "${LOSS_LEVELS[@]}"; do
    for mode in "${MODES[@]}"; do
        for ((rep = 0; rep < REPS; ++rep)); do
            run_one "$loss" "$mode" "$rep"
        done
    done
done

echo "loss sweep complete. results in $EXP_DIR"
