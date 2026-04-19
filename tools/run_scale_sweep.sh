#!/usr/bin/env bash
# Lightning Link scale sweep: clean network, varying the number of connected
# clients (bots) across {1, 2, 4, 6, 8} for both transports. Designed to
# expose bandwidth growth as N rises, verifying the analytical snapshot size
# formula (20 + 28*N bytes) for the optimized path.
#
# Output layout:
#   experiments_scale/N<count>_<mode>_rep<N>/{server,client}_*.csv
# Consumed by: tools/analyze.py --mode scale

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${REPO_DIR}/build"
SERVER="${BUILD_DIR}/lightning_link_server"
CLIENT="${BUILD_DIR}/lightning_link_client"
EXP_DIR="${REPO_DIR}/experiments_scale"

DURATION_SEC="${DURATION_SEC:-30}"
REPS="${REPS:-3}"
BASE_UDP_PORT="${BASE_UDP_PORT:-57421}"
BASE_TCP_PORT="${BASE_TCP_PORT:-57422}"

mkdir -p "$EXP_DIR"

if [[ ! -x "$SERVER" || ! -x "$CLIENT" ]]; then
    echo "error: server/client binaries not found. Run 'cmake --build build -j' first." >&2
    exit 1
fi

CLIENT_COUNTS=(1 2 4 6 8)
MODES=(optimized baseline)

run_one() {
    local n="$1" mode="$2" rep="$3"
    local seed=$((4000 * rep + 19))
    local label="N${n}_${mode}_rep${rep}"
    local out_dir="${EXP_DIR}/${label}"
    rm -rf "$out_dir"
    mkdir -p "$out_dir"

    local udp_port=$((BASE_UDP_PORT + rep))
    local tcp_port=$((BASE_TCP_PORT + rep))
    local srv_dur=$((DURATION_SEC + 2))

    echo ">> scale sweep: mode=${mode} N=${n} rep=${rep}"

    "$SERVER" --mode="$mode" --duration="$srv_dur" \
        --port="$udp_port" --tcp-port="$tcp_port" \
        --run-label="$label" --log-dir="$out_dir" \
        --seed="$seed" \
        >"$out_dir/server.stdout" 2>&1 &
    local srv_pid=$!

    sleep 0.5

    local pids=()
    for ((i = 0; i < n; ++i)); do
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
            --add-latency=0 --loss=0 --seed=$((seed + i + 1)) \
            >"$out_dir/client_${i}.stdout" 2>&1 &
        pids+=($!)
        sleep 0.05
    done

    for pid in "${pids[@]}"; do wait "$pid" || true; done
    wait "$srv_pid" || true

    echo "   done: $(ls "$out_dir"/*.csv 2>/dev/null | wc -l | tr -d ' ') csv files"
}

for n in "${CLIENT_COUNTS[@]}"; do
    for mode in "${MODES[@]}"; do
        for ((rep = 0; rep < REPS; ++rep)); do
            run_one "$n" "$mode" "$rep"
        done
    done
done

echo "scale sweep complete. results in $EXP_DIR"
