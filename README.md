# Lightning Link

A C++20 client-server multiplayer networking prototype that demonstrates
how UDP state synchronization, binary serialization, client-side prediction,
and remote entity interpolation improve real-time responsiveness over a
simple TCP+text baseline — under controlled latency and packet-loss conditions.

Lightning Link is a research-oriented prototype for a final-year project,
not a production game. Scope is intentionally locked: one rectangular arena,
four-direction movement, 2–8 simultaneous players.

## Highlights

- Authoritative server tick at 60 Hz; snapshot broadcast at 20 Hz.
- Two fully-implemented networking modes for A/B evaluation:
  - **Baseline**: TCP + newline-delimited text.
  - **Optimized**: UDP + explicit binary packets + client-side prediction with
    ack-based reconciliation + 100 ms-delayed remote interpolation.
- In-app network conditioner with seeded latency, jitter, and packet loss.
  No `pfctl` or root required; experiments are reproducible across machines.
- Scripted bot clients for automated multi-client stress runs.
- On-screen HUD and authoritative "ghost" overlay for live demos.
- Python analysis pipeline producing publication-ready figures and a CSV
  summary covering all five required test conditions.

## Repository layout

```text
lightning-link/
├── CMakeLists.txt
├── common/          # shared constants, types, protocol, serialization,
│                    # LaggedSocket (UDP conditioner), CSV logging
├── server/          # world state, session manager, UDP snapshot broadcaster,
│                    # TCP baseline server
├── client/          # SFML renderer, input, prediction, interpolation,
│                    # network receiver thread, bot driver, HUD, baseline TCP
├── tools/
│   ├── run_experiments.sh   # 5 conditions x 2 modes x N reps orchestrator
│   ├── run_isolation.sh     # per-optimization ablation at 100 ms RTT
│   ├── run_loss_sweep.sh    # delay-tail behaviour vs packet loss
│   ├── run_scale_sweep.sh   # bandwidth/payload vs player count
│   ├── run_all.sh           # one-shot: all four matrices + analysis
│   └── analyze.py           # --mode main|isolation|loss|scale
├── docs/
│   ├── architecture.md             # mermaid diagrams + source-to-figure index
│   └── results_interpretation.md   # report scaffold, filled from measured data
├── experiments/           # main matrix raw CSVs
├── experiments_isolation/ # isolation matrix raw CSVs (opt-in)
├── experiments_loss/      # loss sweep raw CSVs (opt-in)
├── experiments_scale/     # scale sweep raw CSVs (opt-in)
├── results/               # main matrix figures + summary
├── results_isolation/     # isolation decomposition figure
├── results_loss/          # loss-tolerance figures
├── results_scale/         # scaling figures
└── logs/            # live tail logs from manual runs
```

## Build

### Dependencies

- macOS or Linux, C++20 compiler (tested with AppleClang 17 on Apple Silicon).
- CMake >= 3.22.
- SFML 3 (Homebrew: `brew install sfml`; on Linux use your distro's packages
  or build from source).
- Python 3.10+ with `pandas`, `matplotlib`, `numpy` (only for the analysis step).

### Compile

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build      # runs ll_selftest
```

Produces `build/lightning_link_server` and `build/lightning_link_client`.

## Run (manual)

Start the server (optimized UDP mode) in one terminal:

```bash
./build/lightning_link_server --mode=optimized --port=54321
```

Start one or more clients in other terminals:

```bash
./build/lightning_link_client --mode=optimized --server=127.0.0.1 --port=54321
```

Controls: arrow keys or WASD. Toggle keys:

| Key | Action                                         |
|-----|------------------------------------------------|
| F1  | Show / hide the debug HUD                      |
| F2  | Show / hide the authoritative "ghost" overlay  |
| F3  | Toggle client-side prediction                  |
| F4  | Toggle remote interpolation                    |
| Esc | Close window                                   |

### Baseline (TCP+text) mode

```bash
./build/lightning_link_server --mode=baseline --tcp-port=54322
./build/lightning_link_client --mode=baseline --server=127.0.0.1 --tcp-port=54322
```

### Simulated network conditions

Applied on the client process (so `--add-latency=100` maps directly to 100 ms
of added round-trip time):

```bash
./build/lightning_link_client --mode=optimized --server=127.0.0.1 \
    --add-latency=100 --jitter=10 --loss=3 --seed=42
```

### Headless bot client

```bash
./build/lightning_link_client --mode=optimized --bot=circle --bot-seed=7 \
    --headless --duration=30
```

Available bot patterns: `circle`, `sine`, `zigzag`, `random`.

## Run the evaluation matrix

The full 5 conditions x 2 modes x 3 repetitions matrix:

```bash
DURATION_SEC=60 REPS=3 tools/run_experiments.sh
```

Outputs per-run CSVs under `experiments/<cond>_<mode>_rep<N>/`.

Then produce the results package:

```bash
python3 -m venv .venv && .venv/bin/pip install pandas matplotlib numpy
.venv/bin/python tools/analyze.py
```

Outputs:

- `results/summary.csv`                    — mean and 95% CI per (condition, mode)
- `results/per_run.csv`                    — raw per-run aggregates
- `results/fig0_wire_vs_perceived.png`     — headline wire-vs-perceived contrast
- `results/fig1_perceived_delay.png`       — perceived responsiveness (broken out)
- `results/fig1b_ack_latency.png`          — wire RTT (broken out)
- `results/fig2_bandwidth.png`             — bandwidth per client
- `results/fig3_stability.png`             — snapshot arrival CV (secondary)
- `results/fig3b_arrival_gap_percentiles.png` — p50/p95/p99 of inter-snapshot gaps
- `results/fig4_perceived_vs_latency.png`  — perceived delay as latency scales
- `results/fig6_ack_latency_cdf.png`       — ack latency CDF (appendix)

## Deep analysis (opt-in)

The main matrix tells you "optimized vs baseline overall". For a report-grade
breakdown that attributes the gain to individual optimizations, run one or
more of the following sub-matrices. Each is independent and can be skipped.

To run the entire report pack in one command (main + all three deep matrices,
followed by all four analyzer invocations):

```bash
DURATION_SEC=60 REPS=3 tools/run_all.sh
```

### Isolation matrix (which optimization contributes how much)

Fixes latency at 100 ms RTT and runs five variants: baseline, UDP+binary
only, + prediction, + interpolation, + both. Produces `fig5_isolation_decomp.png`.

```bash
DURATION_SEC=30 REPS=3 tools/run_isolation.sh
.venv/bin/python tools/analyze.py --mode isolation
```

The two relevant client flags introduced for this matrix are
`--disable-prediction` and `--disable-interpolation`. Both can be set
manually for demos.

### Packet-loss sweep (tail behaviour under loss)

Holds latency at 100 ms and sweeps loss across {0, 1, 3, 5, 10}%. Produces
`fig9_loss_delay_tail.png` and `fig10_snapshot_jitter_cv.png`.

```bash
DURATION_SEC=30 REPS=3 tools/run_loss_sweep.sh
.venv/bin/python tools/analyze.py --mode loss
```

### Scale sweep (bandwidth vs player count)

Clean network; N in {1, 2, 4, 6, 8}. Produces `fig7_bytes_per_snapshot.png`
(measured vs analytical `7 + 22*N`) and `fig8_bandwidth_scaling.png`.

```bash
DURATION_SEC=30 REPS=3 tools/run_scale_sweep.sh
.venv/bin/python tools/analyze.py --mode scale
```

Refer to [docs/architecture.md](docs/architecture.md) for a mermaid-diagram
walkthrough and to [docs/results_interpretation.md](docs/results_interpretation.md)
for the report-ready interpretation of every figure.

## Explanation of the two modes

### Baseline (TCP + text)

- Transport: a single TCP connection per client.
- Framing: newline-delimited ASCII.
  - `IN <seq> <mx> <my>` from client (60 Hz).
  - `SS <tick> <n> <id> <x> <y> <vx> <vy> <last_seq> ...` from server (20 Hz).
  - `JA <player_id> <arena_w> <arena_h>` server response to a fresh connection.
  - `BYE` from client on clean exit.
- Rendering: no prediction, no interpolation — the local player waits for each
  authoritative snapshot before the input is visible.
- Deliberately unoptimized: it is the comparator that the optimized mode is
  measured against. Same send rates as optimized for fair A/B.

### Optimized (UDP + binary + prediction + interpolation)

- Transport: UDP, one socket per endpoint.
- Framing: explicit binary packets defined in `common/protocol.hpp` with
  bounds-checked `BufferWriter` / `BufferReader` and network byte order. All
  floats are transmitted as IEEE-754 binary32 in network byte order.
- Client-side prediction: the local player's input is applied immediately
  (zero perceived latency). A ring buffer of the last 120 inputs is retained.
  On every `WORLD_SNAPSHOT`, the client snaps to the authoritative state and
  replays inputs whose sequence number exceeds the server's reported
  `last_processed_input_seq`.
- Remote interpolation: each remote entity is rendered 100 ms behind the
  latest wire-time, linearly interpolated between the two bracketing snapshots.
- In-app network conditioner: `LaggedSocket` delays and drops datagrams per
  a seeded PRNG so the evaluation matrix is reproducible without platform
  traffic shaping tools.

## Experiment conditions

| Label           | Added RTT | Loss | Clients |
|-----------------|-----------|------|---------|
| A_clean         | 0 ms      | 0 %  | 2       |
| B_lat100        | 100 ms    | 0 %  | 2       |
| C_lat200        | 200 ms    | 0 %  | 2       |
| D_lat100_loss3  | 100 ms    | 3 %  | 2       |
| E_stress_8      | 100 ms    | 0 %  | 8       |

Run label, seed, client count, latency, and loss are all fixed by
`tools/run_experiments.sh` for reproducibility.

## Log file locations

- Live manual runs: `logs/server_<label>_<timestamp>.csv` and
  `logs/client_<label>_p<pid>_{periodic,inputs}_<timestamp>.csv`.
- Experiment runs: `experiments/<run>/server_*.csv`,
  `experiments/<run>/client_*_periodic_*.csv`,
  `experiments/<run>/client_*_inputs_*.csv`.

## Known limitations

- IPv4 only (spec scope-locked to desktop development).
- Baseline and optimized experiments are run sequentially (not concurrently)
  by the orchestrator, to keep ports free and remove cross-contamination.
- The in-app conditioner applies latency per-datagram for UDP and per-line
  for TCP. TCP-native effects (head-of-line blocking under loss) are not
  re-simulated because the baseline mode at localhost does not exhibit them.
- The prediction model has no rollback, lag compensation, or delta
  reconciliation.
- No WAN deployment, matchmaking, persistence, authentication, combat, or
  projectile synchronisation.
