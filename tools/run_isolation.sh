#!/usr/bin/env bash
# Lightning Link isolation matrix: at a single network condition (100 ms RTT,
# 0% loss, 2 clients) we toggle individual optimizations so the report can
# attribute measured gains to specific architectural choices.
#
# Five variants are executed:
#   baseline           - TCP + text protocol (reference)
#   optimized_raw      - UDP + binary, no prediction, no interpolation
#   optimized_noInterp - UDP + binary + prediction (no interpolation)
#   optimized_noPred   - UDP + binary + interpolation (no prediction)
#   optimized          - full stack (UDP + binary + prediction + interpolation)
#
# Output layout:
#   experiments_isolation/<variant>_rep<N>/{server,client}_*.csv
# Consumed by: tools/analyze.py --mode isolation

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${REPO_DIR}/build"
SERVER="${BUILD_DIR}/lightning_link_server"
CLIENT="${BUILD_DIR}/lightning_link_client"
EXP_DIR="${REPO_DIR}/experiments_isolation"

DURATION_SEC="${DURATION_SEC:-30}"
REPS="${REPS:-3}"
BASE_UDP_PORT="${BASE_UDP_PORT:-55421}"
BASE_TCP_PORT="${BASE_TCP_PORT:-55422}"
LATENCY_MS="${LATENCY_MS:-100}"
LOSS_PCT="${LOSS_PCT:-0}"
CLIENT_COUNT="${CLIENT_COUNT:-2}"

mkdir -p "$EXP_DIR"

if [[ ! -x "$SERVER" || ! -x "$CLIENT" ]]; then
    echo "error: server/client binaries not found. Run 'cmake --build build -j' first." >&2
    exit 1
fi

# variant_name | mode | extra client flags
VARIANTS=(
    "baseline|baseline|"
    "optimized_raw|optimized|--disable-prediction --disable-interpolation"
    "optimized_noInterp|optimized|--disable-interpolation"
    "optimized_noPred|optimized|--disable-prediction"
    "optimized|optimized|"
)

run_one() {
    local variant="$1" mode="$2" extra="$3" rep="$4"
    local seed=$((2000 * rep + 11))
    local label="${variant}_rep${rep}"
    local out_dir="${EXP_DIR}/${label}"
    rm -rf "$out_dir"
    mkdir -p "$out_dir"

    local udp_port=$((BASE_UDP_PORT + rep))
    local tcp_port=$((BASE_TCP_PORT + rep))
    local srv_dur=$((DURATION_SEC + 2))

    echo ">> isolation: variant=${variant} rep=${rep} lat=${LATENCY_MS}ms loss=${LOSS_PCT}%"

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
        # shellcheck disable=SC2086
        "$CLIENT" --mode="$mode" --server=127.0.0.1 \
            --port="$udp_port" --tcp-port="$tcp_port" \
            --duration="$DURATION_SEC" \
            --run-label="$label" --log-dir="$out_dir" \
            --bot="$bot_pattern" --bot-seed=$((seed + i)) --headless \
            --add-latency="$LATENCY_MS" --loss="$LOSS_PCT" --seed=$((seed + i + 1)) \
            $extra \
            >"$out_dir/client_${i}.stdout" 2>&1 &
        pids+=($!)
        sleep 0.05
    done

    for pid in "${pids[@]}"; do wait "$pid" || true; done
    wait "$srv_pid" || true

    echo "   done: $(ls "$out_dir"/*.csv 2>/dev/null | wc -l | tr -d ' ') csv files"
}

for entry in "${VARIANTS[@]}"; do
    IFS='|' read -r variant mode extra <<< "$entry"
    for ((rep = 0; rep < REPS; ++rep)); do
        run_one "$variant" "$mode" "$extra" "$rep"
    done
done

echo "isolation matrix complete. results in $EXP_DIR"
