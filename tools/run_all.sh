#!/usr/bin/env bash
# Lightning Link one-shot evaluator. Runs the main matrix plus all three
# deep-analysis matrices, then produces every figure in the report pack.
#
# Honours the same env vars each sub-script accepts; the important knob is
# DURATION_SEC (defaults to 30, matching the sub-scripts' defaults).
#
# Typical report run:
#   DURATION_SEC=60 REPS=3 tools/run_all.sh
#
# Produces:
#   experiments/            - main matrix raw CSVs
#   experiments_isolation/  - isolation matrix raw CSVs
#   experiments_loss/       - loss-sweep raw CSVs
#   experiments_scale/      - scale-sweep raw CSVs
#   results/                - figs 0, 1, 1b, 2, 3, 3b, 4, 6
#   results_isolation/      - fig5
#   results_loss/           - fig9, fig10
#   results_scale/          - fig7, fig8

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${REPO_DIR}/build"
if [[ ! -x "${BUILD_DIR}/lightning_link_server" || ! -x "${BUILD_DIR}/lightning_link_client" ]]; then
    echo "error: server/client binaries not found. Run 'cmake --build build -j' first." >&2
    exit 1
fi

PY="${PY:-${REPO_DIR}/.venv/bin/python}"
if [[ ! -x "$PY" ]]; then
    PY="$(command -v python3)"
fi

step() { echo; echo "=== $* ==="; echo; }

step "1/4  main matrix (5 conditions x 2 modes)"
"$SCRIPT_DIR/run_experiments.sh"

step "2/4  isolation matrix (per-optimization ablation)"
"$SCRIPT_DIR/run_isolation.sh"

step "3/4  loss sweep (0, 1, 3, 5, 10%)"
"$SCRIPT_DIR/run_loss_sweep.sh"

step "4/4  scale sweep (N in {1, 2, 4, 6, 8})"
"$SCRIPT_DIR/run_scale_sweep.sh"

step "analyzing main"
"$PY" "$SCRIPT_DIR/analyze.py" --mode main

step "analyzing isolation"
"$PY" "$SCRIPT_DIR/analyze.py" --mode isolation

step "analyzing loss"
"$PY" "$SCRIPT_DIR/analyze.py" --mode loss

step "analyzing scale"
"$PY" "$SCRIPT_DIR/analyze.py" --mode scale

echo
echo "all done. figures written under results*/ ."
