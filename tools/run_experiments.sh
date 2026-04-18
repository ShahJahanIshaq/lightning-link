#!/usr/bin/env bash
# Lightning Link experiment orchestrator.
#
# Runs the 5 test conditions (A-E) x 2 modes (optimized, baseline) x REPS repetitions
# with deterministic seeds. All runs use in-app conditioning via the client and
# server --add-latency / --loss flags so results are reproducible across machines.
#
# Output layout:
#   experiments/<condition>_<mode>_<rep>/
#     server_*.csv
#     client_*_periodic_*.csv
#     client_*_inputs_*.csv
#
# The resulting directories are consumed by tools/analyze.py.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${REPO_DIR}/build"
SERVER="${BUILD_DIR}/lightning_link_server"
CLIENT="${BUILD_DIR}/lightning_link_client"
EXP_DIR="${REPO_DIR}/experiments"

DURATION_SEC="${DURATION_SEC:-30}"
REPS="${REPS:-3}"
BASE_UDP_PORT="${BASE_UDP_PORT:-54321}"
BASE_TCP_PORT="${BASE_TCP_PORT:-54322}"

mkdir -p "$EXP_DIR"

if [[ ! -x "$SERVER" || ! -x "$CLIENT" ]]; then
    echo "error: server/client binaries not found. Run 'cmake --build build -j' first." >&2
    exit 1
fi

# condition_name | add-latency ms | loss pct | client_count
CONDITIONS=(
    "A_clean|0|0|2"
    "B_lat100|100|0|2"
    "C_lat200|200|0|2"
    "D_lat100_loss3|100|3|2"
    "E_stress_8|100|0|8"
)

MODES=(optimized baseline)

run_one() {
    local cond="$1" lat="$2" loss="$3" clients="$4" mode="$5" rep="$6"
    local seed=$((1000 * rep + 7))
    local label="${cond}_${mode}_rep${rep}"
    local out_dir="${EXP_DIR}/${label}"
    rm -rf "$out_dir"
    mkdir -p "$out_dir"

    local udp_port=$((BASE_UDP_PORT + rep))
    local tcp_port=$((BASE_TCP_PORT + rep))

    echo ">> running $label (lat=${lat}ms loss=${loss}% clients=${clients})"

    # Server duration slightly longer than client duration to absorb shutdown lag.
    local srv_dur=$((DURATION_SEC + 2))

    local mode_flag="--mode=${mode}"

    # Conditioning is applied ONLY to clients so that --add-latency=X maps directly
    # to X ms of added round-trip time (half on outbound, half on inbound at one
    # endpoint). The server runs clean.
    "$SERVER" $mode_flag --duration="$srv_dur" \
        --port="$udp_port" --tcp-port="$tcp_port" \
        --run-label="$label" --log-dir="$out_dir" \
        --seed="$seed" \
        >"$out_dir/server.stdout" 2>&1 &
    local srv_pid=$!

    sleep 0.5

    local pids=()
    for ((i = 0; i < clients; ++i)); do
        local bot_pattern
        case $((i % 4)) in
            0) bot_pattern=circle ;;
            1) bot_pattern=sine   ;;
            2) bot_pattern=zigzag ;;
            *) bot_pattern=random ;;
        esac
        "$CLIENT" $mode_flag --server=127.0.0.1 \
            --port="$udp_port" --tcp-port="$tcp_port" \
            --duration="$DURATION_SEC" \
            --run-label="$label" --log-dir="$out_dir" \
            --bot="$bot_pattern" --bot-seed=$((seed + i)) --headless \
            --add-latency="$lat" --loss="$loss" --seed=$((seed + i + 1)) \
            >"$out_dir/client_${i}.stdout" 2>&1 &
        pids+=($!)
        sleep 0.05
    done

    for pid in "${pids[@]}"; do wait "$pid" || true; done
    wait "$srv_pid" || true

    echo "   done: $(ls "$out_dir"/*.csv 2>/dev/null | wc -l | tr -d ' ') csv files"
}

for entry in "${CONDITIONS[@]}"; do
    IFS='|' read -r cond lat loss clients <<< "$entry"
    for mode in "${MODES[@]}"; do
        for ((rep = 0; rep < REPS; ++rep)); do
            run_one "$cond" "$lat" "$loss" "$clients" "$mode" "$rep"
        done
    done
done

echo "all experiments complete. results in $EXP_DIR"
