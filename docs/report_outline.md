# Lightning Link - FYP Final Report Outline

This outline follows the five chapters required by the department's
`finalreport_guideline_general.pdf`. Subsections are chosen to match
what actually exists in this repository: the two-mode architecture
(optimized UDP + binary + prediction + interpolation vs. baseline
TCP + text), the four experiment matrices (main / isolation / loss /
scale), and the figure set produced by `tools/analyze.py`.

---

## Chapter 1. Introduction

1.1 Project background
  - 1.1.1 Real-time networked interactive systems as a domain
  - 1.1.2 The latency problem in competitive multiplayer games
  - 1.1.3 Why transport and client-side techniques matter as much as raw bandwidth

1.2 Project objectives
  - 1.2.1 Build a playable client-server 2D game from scratch in C++20
  - 1.2.2 Implement a "baseline" TCP text protocol with no responsiveness tricks
  - 1.2.3 Implement an "optimized" UDP binary protocol with prediction and interpolation
  - 1.2.4 Quantify the difference with a reproducible, scripted evaluation pipeline

1.3 Requirements
  - 1.3.1 Functional requirements (join, move, see other players, leave, two-mode parity)
  - 1.3.2 Non-functional requirements (determinism, reproducibility, measurability, portability across macOS/Linux)
  - 1.3.3 Scope boundaries (single LAN process space, up to 8 clients, 2D arena, no security layer)

1.4 Contributions
  - 1.4.1 A complete two-mode prototype in a single repository (`client/`, `server/`, `common/`)
  - 1.4.2 An in-application network conditioner (`common/lagged_socket.*`) that makes both modes comparable without kernel tooling
  - 1.4.3 A headless bot harness (`client/bot.*`) that enables automated experiments
  - 1.4.4 A four-matrix evaluation framework (`tools/run_*.sh`) and an analyzer (`tools/analyze.py`) that produces report-ready figures
  - 1.4.5 Quantitative findings decoupling *wire latency* from *perceived latency*

1.5 Report structure

---

## Chapter 2. Project Background and Literature Review

### 2.1 Project background: the real-time multiplayer problem

2.1.1 Domain and scale
  - Growth of real-time multiplayer as a deployment class (games, collaborative tools, virtual events, telepresence, industrial digital twins)
  - Shared characteristic: a distributed simulation must stay consistent across participants on an unreliable public network while feeling instantaneous to each user
  - Why this is hard in one sentence: the speed of light plus routing reality gives ~30-200 ms of round-trip delay on a WAN, which is enough to break the illusion of local action if handled naively

2.1.2 What "responsive" means quantitatively
  - Perceptual thresholds cited in HCI literature (~100 ms for action-to-feedback, ~20 ms for competitive fairness)
  - Why a naive client-server design cannot meet these thresholds without extra techniques, even on a good network

2.1.3 The anatomy of a networked game loop (framing for the rest of the chapter)
  - Input capture, authoritative simulation, state propagation, local rendering
  - Each stage is a potential source of added latency, jitter, or inconsistency

### 2.2 What is available today

2.2.1 Commercial game engines with built-in networking
  - Unreal Engine replication model (actors, RPCs, server-authoritative)
  - Unity Netcode for GameObjects / Mirror / Fish-Net
  - Source Engine / Source 2 networking (snapshots, entity baselines)

2.2.2 Open-source networking libraries
  - ENet (reliable/unreliable channels over UDP)
  - Valve GameNetworkingSockets (UDP + encryption + reliability)
  - yojimbo / netcode.io (security-focused UDP framing)
  - RakNet / SLikeNet (legacy but influential)
  - QUIC-based experimentation (reliable UDP with congestion control)

2.2.3 Research and reference implementations
  - Quake 3 Arena network model (open-sourced, first mainstream reference for prediction + snapshots)
  - Valve's "Source Multiplayer Networking" and "Latency Compensating Methods in Client/Server In-Game Protocol Design and Optimization" (Bernier, 2001)
  - Gabriel Gambetta's "Fast-Paced Multiplayer" tutorial series (widely cited educational reference)
  - Glenn Fiedler's "Gaffer on Games" articles (prediction, interpolation, reliability over UDP)

2.2.4 Adjacent transport-level infrastructure
  - DTLS, QUIC, WebRTC data channels
  - Linux `tc netem` and equivalent tools for WAN emulation
  - Public playtest metric pipelines (Riot's network telemetry papers, for example)

### 2.3 Shortcomings of what is available

2.3.1 Commercial engines are black boxes for teaching purposes
  - The exact cost of each optimization is opaque; one cannot disable "just prediction" or "just interpolation" and measure the delta
  - Engine defaults couple several optimizations together, so their individual contributions cannot be attributed without source access
  - License and size make them unsuitable as a teaching artefact for a final year project

2.3.2 Open-source libraries solve transport, not the game loop
  - ENet / GameNetworkingSockets provide reliable/unreliable UDP framing but leave prediction, interpolation, and reconciliation to the application
  - Documentation tends to be API-focused, not pedagogical; the reader still has to implement the interesting part
  - They cannot be toggled to "TCP text baseline" for a side-by-side comparison

2.3.3 Existing tutorials are descriptive, not measured
  - Gambetta, Fiedler, Bernier explain the techniques clearly but do not quantify them against a baseline under controlled conditions
  - Most online examples are JavaScript/browser demos; there is no end-to-end C++ reference small enough to read in a weekend
  - None that the author is aware of publish a reproducible evaluation script alongside the prototype

2.3.4 Academic evaluations are fragmented
  - Most published measurements focus on one variable (for example, prediction only, or transport only) rather than decomposing the total stack
  - Results are often reported at a single operating point (one RTT, one loss rate, one player count) rather than across matrices
  - Few papers publish their harness or their raw data

### 2.4 Why undertake this project (motivation)

2.4.1 Personal learning motivation
  - Primary driver: building a working system end-to-end is the only reliable way to internalise the university-level systems curriculum rather than revisit it at exam depth
  - Computer networks: moving from textbook descriptions of the OSI model to actually using BSD sockets, choosing between TCP and UDP, writing a wire format, and reasoning about RTT, jitter, and loss as live variables rather than quiz topics (exercised by `common/net_compat.*`, `common/serialization.*`, `common/lagged_socket.*`)
  - Operating systems: engaging directly with processes, threads, blocking vs non-blocking I/O, `select`/timeouts, signal handling, and how the kernel schedules a multi-threaded server and client (exercised by `server/server.cpp`, `client/network_receiver.*`, the shutdown paths in `server/main.cpp` and `client/main.cpp`)
  - Concurrency: hands-on practice with `std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic`, producer-consumer queues, and the class of bugs that only appear under contention (exercised by `client/snapshot_queue.hpp` and the server broadcaster)
  - Systems programming in modern C++: ownership, RAII, move semantics, header/source discipline, and the difference between code that compiles and code that behaves predictably under load
  - Build systems and toolchain: CMake, dependency management (SFML 3), cross-platform portability (macOS/Linux), and the developer-experience cost of each decision
  - Software engineering at scale: designing a codebase larger than a single-file assignment, splitting responsibilities across modules, keeping a deliberately-parallel baseline alongside the optimized path, and maintaining `docs/deviations.md` as an honest log of what changed and why
  - Experimental computer science: designing a reproducible harness, choosing metrics, replicating runs, reporting confidence intervals, and resisting the temptation to cherry-pick results (exercised by `tools/run_*.sh` and `tools/analyze.py`)
  - Scientific writing and data literacy: turning a pile of CSVs into figures that defend a specific claim, and knowing which figures do not support the claim the student originally expected

2.4.2 Pedagogical motivation (for readers of the final artefact)
  - A self-contained C++20 codebase, small enough that a reader can understand every line, is a better teaching artefact than any production engine
  - Building the techniques from the socket up is the only way to understand where each millisecond of perceived delay goes

2.4.3 Methodological motivation
  - The literature's descriptive style ("prediction helps") can be replaced by quantitative claims ("at 100 ms RTT, prediction reduces perceived delay from ~130 ms to ~0 ms; UDP+binary reduces bandwidth per client by X%")
  - CLI ablation flags (`--disable-prediction`, `--disable-interpolation`) plus scripted experiment matrices allow each optimization to be evaluated in isolation

2.4.4 Practical motivation
  - The same techniques studied here apply beyond games to any soft-real-time distributed interactive system (remote control, tele-operation, shared editors)
  - Having a reproducible baseline-vs-optimized harness makes it a useful sandbox for testing further protocol ideas

### 2.5 Gap this project addresses

2.5.1 A single, readable codebase that implements both a deliberately-naive baseline and an optimized path, so a reader can diff them literally
2.5.2 Per-optimization ablation built into the client CLI, enabling attribution of each millisecond of improvement to a specific technique
2.5.3 Four orchestrated experiment matrices (main, isolation, loss sweep, scale sweep) producing a report-ready figure set with one command
2.5.4 Open raw data (`experiments*/` CSVs) and analysis scripts alongside the figures, so the numbers can be re-derived and contested

---

## Chapter 3. Project Methodology

The chapter is organised around the journey of an input through the system,
not around the directory layout of the source. Section 3.1 fixes the
architectural vocabulary; sections 3.2 and 3.3 describe the shared
substrate (sockets, conditioner, logging) and the wire protocols that sit
on top of it; sections 3.4 to 3.6 describe the server, the client's
structural plumbing, and the client-side latency-hiding techniques that
constitute the report's intellectual centre; section 3.7 gives a unified
view of the concurrency model now that both sides have been described;
sections 3.8 to 3.10 cover instrumentation, the build, and the
experimental pipeline that drives the system through chapter 4.

**Style note for this chapter (and chapter 4).** Components are referred
to by their conceptual name (e.g. *the predictor*, *the in-application
network conditioner*, *the snapshot broadcaster*) rather than by their
source-file path or CLI flag. Directory names appear only in the module
map (3.1.3); build target names only in the build section (3.9.2);
shell scripts only by their conceptual role in the evaluation pipeline
(3.10). The full file listings, exhaustive CLI flag reference, and
exact byte-level wire-format tables live in the appendices, and are
pointed to from the body where useful but never quoted in full inline.
File-path mentions in the outline below are planning anchors; they
should not appear verbatim in the report prose.

3.1 System architecture
  - 3.1.1 Two-mode design and shared code (optimized UDP vs baseline TCP)
  - 3.1.2 Process topology: one server, N clients, optional per-client bot
  - 3.1.3 Module map (`common/`, `server/`, `client/`) and component diagram

3.2 Shared foundations (`common/`)
  - 3.2.1 Simulation constants and shared types (`constants.hpp`, `types.hpp`, with `math_utils.hpp` mentioned in passing)
  - 3.2.2 Cross-platform socket compatibility layer (`net_compat.*`)
  - 3.2.3 In-application network conditioner (`lagged_socket.*`) and its parameterisation
  - 3.2.4 CSV logging utilities (`logging.*`) and the rolling-flush model
  - 3.2.5 Self-test target (`selftest.cpp`) as a correctness contract for the wire format

3.3 Wire protocols
  - 3.3.1 Message taxonomy common to both modes (`JOIN_REQUEST`, `JOIN_ACCEPT`, `INPUT_COMMAND`, `WORLD_SNAPSHOT`, `DISCONNECT_NOTICE`)
  - 3.3.2 Optimized binary format: 7-byte header, 22-byte `PLAYER_STATE`, bounds-checked `BufferReader`/`BufferWriter`
  - 3.3.3 Sequence numbering and the `last_processed_input_seq` ack field as the basis for client reconciliation
  - 3.3.4 Baseline text protocol: line grammar, lossless equivalence to the binary form, deliberate verbosity
  - 3.3.5 Side-by-side comparison: per-message byte counts, encode/decode complexity, where the bandwidth difference comes from

3.4 Authoritative server (`server/`)
  - 3.4.1 Fixed-timestep simulation (`world_state.*`, `step_tick`, deterministic kinematics)
  - 3.4.2 Session lifecycle: endpoint binding, stale-sequence rejection, liveness reaping (`session_manager.*`)
  - 3.4.3 Snapshot broadcaster for optimized mode (`udp_snapshot_broadcaster.*`)
  - 3.4.4 Baseline TCP server path (`tcp_baseline.cpp`) and its parallelism with the optimized path
  - 3.4.5 Server event loop, mode selection, and CLI surface (`server.*`, `main.cpp`)

3.5 Client structure and rendering (`client/`)
  - 3.5.1 Entry point, `ClientConfig`, and CLI surface (`main.cpp`)
  - 3.5.2 Keyboard sampling, window-focus gating, and the bot harness (`input.*`, `bot.*`, `client.cpp`)
  - 3.5.3 Renderer and HUD (`renderer.*`, `hud.*`, SFML integration; the F1/F2/F3/F4 toggle surface)
  - 3.5.4 Baseline TCP client path (`tcp_baseline_client.cpp`) and its parallelism with the optimized path

3.6 Client-side latency hiding (the techniques)
  - 3.6.1 Network receiver thread and snapshot queue (`network_receiver.*`, `snapshot_queue.hpp`)
  - 3.6.2 Client-side prediction: input ring buffer, local advance, and the `record_and_step` interface (`prediction.*`)
  - 3.6.3 Server reconciliation: snap-to-authoritative and replay of unacknowledged inputs, with `reconciliation_error_px` as a runtime self-check
  - 3.6.4 Entity interpolation for remote players: 100 ms buffer, linear interpolation, freshness-vs-smoothness trade-off (`interpolation.*`)
  - 3.6.5 Ablation surface: startup switches and runtime toggles for prediction and interpolation, and the mode-label values the analyzer keys on

3.7 Concurrency model (cross-module view)
  - 3.7.1 Server threads: tick loop, UDP listen loop, snapshot broadcaster, baseline TCP accept loop
  - 3.7.2 Client threads: main/render thread, network receiver thread, optional bot driver
  - 3.7.3 Synchronization primitives in use (`std::mutex`, `std::condition_variable`, `std::atomic`) and the rationale for each
  - 3.7.4 The producer-consumer snapshot queue as the only cross-thread data path on the client
  - 3.7.5 Shutdown coordination via `Server::stop_` and signal handlers

3.8 Instrumentation and observability
  - 3.8.1 What is measured and why: distinguishing wire RTT (`rtt_ms`) from perceived delay (`perceived_delay_ms`)
  - 3.8.2 Server periodic CSV log: tick, player count, bytes sent, snapshots emitted
  - 3.8.3 Client periodic CSV log: `rtt_ms`, `perceived_delay_ms`, interpolation buffer depth, mode label
  - 3.8.4 Per-input CSV log: `wall_time_ms`, `input_seq`, `ack_seq`, `rtt_ms`, `perceived_delay_ms`, `reconciliation_error_px`, `mode`
  - 3.8.5 Mapping from log columns to the figures in chapter 4 (the source-to-figure index)

3.9 Build system and reproducibility
  - 3.9.1 CMake configuration, C++20 toolchain, and the SFML 3 dependency
  - 3.9.2 Build targets: `lightning_link_server`, `lightning_link_client`, `ll_selftest`
  - 3.9.3 Repository layout and where each artefact lives
  - 3.9.4 Specification deviations log (`docs/deviations.md`) as the honesty mechanism for NFR-8

3.10 Evaluation pipeline
  - 3.10.1 Shell orchestrators: `run_experiments.sh`, `run_isolation.sh`, `run_loss_sweep.sh`, `run_scale_sweep.sh`, and the `run_all.sh` one-shot
  - 3.10.2 Bot harness as the experimental driver (cross-reference back to 3.5.2)
  - 3.10.3 Python analyzer (`tools/analyze.py`) and its four modes (`main | isolation | loss | scale`)
  - 3.10.4 Aggregation, statistics, and figure generation: replications, 95% CI via `_agg_stat`, output directory layout

---

## Chapter 4. Experiments and Results

4.1 Experimental setup
  - 4.1.1 Hardware and OS (macOS, Apple Silicon; loopback transport)
  - 4.1.2 Build configuration (Release, C++20, SFML 3.0.0)
  - 4.1.3 Deterministic bot workloads (`client/bot.*`)
  - 4.1.4 In-app network conditioner parameters (added RTT, jitter, loss)
  - 4.1.5 Default run parameters (`DURATION_SEC`, `REPS`) and aggregation rules

4.2 Experiment matrices
  - 4.2.1 Main matrix: five conditions (A_clean, B_lat100, C_lat200, D_lat100_loss3, E_stress_8) x two modes
  - 4.2.2 Isolation matrix: five variants (baseline, optimized_raw, optimized_noInterp, optimized_noPred, optimized) at B_lat100
  - 4.2.3 Loss sweep: {0, 1, 3, 5, 10}% at 100 ms RTT
  - 4.2.4 Scale sweep: N in {1, 2, 4, 6, 8} on a clean network

4.3 Main-matrix results
  - 4.3.1 Headline: wire RTT vs perceived delay (fig0)
  - 4.3.2 Perceived responsiveness by condition (fig1)
  - 4.3.3 Ack latency by condition (fig1b)
  - 4.3.4 Bandwidth per client (fig2)
  - 4.3.5 Snapshot arrival stability: CV (fig3) and percentile gaps (fig3b)
  - 4.3.6 Perceived delay as a function of added RTT (fig4)
  - 4.3.7 Ack-latency CDF - appendix figure (fig6)
  - 4.3.8 Numerical summary from `results/summary.csv`

4.4 Isolation-matrix results (per-optimization decomposition)
  - 4.4.1 Rationale: attributing gain to prediction vs interpolation vs transport vs binary encoding
  - 4.4.2 Findings (fig5) and interpretation from `docs/results_interpretation.md` section 9

4.5 Loss-sweep results
  - 4.5.1 Delay tail versus loss rate (fig9)
  - 4.5.2 Snapshot jitter coefficient versus loss rate (fig10)
  - 4.5.3 Interpretation: TCP retransmit tail vs UDP+interpolation absorption

4.6 Scale-sweep results
  - 4.6.1 Bytes per snapshot, measured vs analytical `7 + 22N` (fig7)
  - 4.6.2 Total bandwidth scaling (fig8)
  - 4.6.3 Discussion of `O(N)` per client and `O(N^2)` aggregate

4.7 Cross-experiment synthesis
  - 4.7.1 Claims-to-figures mapping (reproduces the table from `docs/results_interpretation.md`)
  - 4.7.2 What each optimization buys under which condition

4.8 Threats to validity
  - 4.8.1 Loopback-only networking; no kernel-level impairment
  - 4.8.2 In-application conditioner approximations (uniform jitter, independent loss)
  - 4.8.3 Short run durations and limited replication count (user-configurable)
  - 4.8.4 Single-machine resource contention between server and bots
  - 4.8.5 Deterministic bot patterns do not cover adversarial inputs

---

## Chapter 5. Conclusion and Future Work

5.1 Summary of findings
  - 5.1.1 Prediction, not transport, is the dominant source of perceived-delay reduction
  - 5.1.2 UDP+binary produces the majority of the bandwidth improvement
  - 5.1.3 Interpolation absorbs wire-level jitter without a visible stall
  - 5.1.4 Baseline TCP degrades faster than optimized UDP under loss

5.2 Limitations of the current prototype
  - 5.2.1 No encryption, authentication, or NAT traversal
  - 5.2.2 No delta compression; full snapshots every tick
  - 5.2.3 No server-side lag compensation (hit-scan / rewind)
  - 5.2.4 2D arena without physics or collision nuance
  - 5.2.5 Single-region deployment assumed

5.3 Future work
  - 5.3.1 Delta encoding and per-client snapshot diffing
  - 5.3.2 DTLS or libsodium for wire confidentiality
  - 5.3.3 Server-side lag compensation and hit-rewind for shooter-style interactions
  - 5.3.4 Adaptive interpolation buffer based on measured jitter
  - 5.3.5 Extension to 3D / physics / area-of-interest culling
  - 5.3.6 Real WAN measurements with a distributed bot swarm
  - 5.3.7 Integration with open-source game-networking libraries (GameNetworkingSockets, ENet) for comparison

5.4 Reflection on learning outcomes
  - 5.4.1 Networking: what BSD sockets, UDP vs TCP, and wire-format design felt like in practice compared to how they are taught
  - 5.4.2 Operating systems: the reality of threading, blocking I/O with timeouts, and shutdown races (and how they differ from the textbook account)
  - 5.4.3 Concurrency: bugs that only appear under contention, and how `std::atomic` + condition variables + a single-writer queue were chosen as the minimum viable primitive set
  - 5.4.4 Systems programming in modern C++: what RAII and ownership bought, and what they cost at the boundaries (SFML 3 migration, cross-platform sockets)
  - 5.4.5 Experimental practice: designing the matrices, interpreting surprising results honestly (the arrival-gap CV that initially looked "worse" for optimized), and the discipline of separating wire metrics from user-perceived metrics
  - 5.4.6 What turned out harder than expected (cross-platform sockets, SFML 3 migration, focus-gated input, avoiding cherry-picked findings)
  - 5.4.7 What generalises beyond games (any soft-real-time distributed interactive system: remote control, tele-operation, shared editors)

---

## Appendices (suggested)

A. Build and run instructions (cross-references `README.md`)
B. Full CLI flag reference for `lightning_link_server` and `lightning_link_client`
C. Protocol wire format tables (byte-level layout of each message)
D. Full figure gallery including appendix figures (fig6)
E. Deviations from the original specification (reproduces `docs/deviations.md`)
F. Raw `summary.csv` tables per matrix
