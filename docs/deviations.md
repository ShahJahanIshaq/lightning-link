# Deviations from the original specification

All deviations are deliberate, in-scope, and intended to make the evaluation
defensible for a final-year project report. The original specification is
preserved verbatim at [project_specification.md](project_specification.md).

## 1. Ack-field reconciliation (PLAYER_STATE.last_processed_input_seq)

**Spec**: "server acknowledgements shall be approximated by snapshot tick
ordering rather than a dedicated ack field" (section "Fixed Client Prediction
Rules").

**Deviation**: `PLAYER_STATE` now carries a `uint32 last_processed_input_seq`
field. The client reconciles by snapping to the authoritative state and
replaying inputs whose sequence number is strictly greater than this value.

**Why**: Without an explicit ack, the client either re-applies already-processed
inputs (drift / rubber-banding) or discards valid unacknowledged ones. The
Gambetta/Gaffer-on-Games reconciliation model cited in the spec's research
objective requires this field. The cost is 4 bytes per player per snapshot
(< 3 % of snapshot size at 8 players) and it is implemented in the identical
way in both the optimized and baseline protocols, so the A/B comparison
remains fair.

## 2. Baseline TCP protocol pinned precisely

**Spec**: "text-based message serialization" (section "Fixed Networking Design").

**Deviation**: The baseline protocol is pinned to newline-delimited ASCII
with exact grammar:
- `IN <seq> <mx> <my>` (client -> server, 60 Hz).
- `SS <tick> <n> <id> <x> <y> <vx> <vy> <last_seq> ...` (server -> client, 20 Hz).
- `JA <player_id> <arena_w> <arena_h>` (server -> client on join).
- `BYE` (client -> server on clean exit).

**Why**: "Text-based" alone is under-specified and would make A/B comparisons
meaningless (different encoders produce different bandwidths). Pinning the
format ensures the comparison measures transport+serialization+prediction/
interpolation, not message-density choices.

## 3. In-app network conditioner (LaggedSocket + baseline TCP conditioner)

**Spec**: test conditions list latency and loss but leave the enforcement
mechanism unspecified.

**Deviation**: added `common/lagged_socket.hpp/.cpp` (UDP) and a parallel
line-level delay/loss queue in the baseline TCP client. Both are driven
by a seeded `std::mt19937_64` so every experiment is bit-for-bit reproducible.
CLI flags: `--add-latency=MS`, `--jitter=MS`, `--loss=PCT`, `--seed=N`.

**Why**: Platform traffic-shaping tools differ (`tc` on Linux, `pfctl`/`dnctl`
on macOS, Clumsy on Windows) and some require root. The in-app conditioner is
reproducible on every target without privilege escalation and keeps the whole
experiment pipeline self-contained.

## 4. Endpoint-bound sessions and input-sequence validation

**Spec**: "session management" is not described in detail.

**Deviation**: the server keys sessions on the UDP `(ip, port)` tuple and
validates that an incoming `INPUT_COMMAND`'s `player_id` matches the session.
The server also tracks the latest sequence number per player and drops
strictly-older UDP inputs (out-of-order delivery guard).

**Why**: Without endpoint binding, any client can spoof another player's ID.
Without stale-seq drop, reordered UDP datagrams can overwrite newer inputs.
Both are one-line fixes that make the prototype behave defensibly.

## 5. Client liveness timeout

**Spec**: a `DISCONNECT_NOTICE` packet exists but there is no liveness rule.

**Deviation**: the server drops any client with no inputs for 3 seconds
(sessions are reaped in the main tick loop and their players removed from
the world state). This is heartbeat-free since clients send inputs at 60 Hz.

**Why**: UDP clients can vanish silently; without a timeout the server
accumulates zombie sessions.

## 6. `tick_hint` removed from `INPUT_COMMAND`

**Spec**: `INPUT_COMMAND` included a `uint32 tick_hint` with no defined
semantics.

**Deviation**: `tick_hint` is removed. `INPUT_COMMAND` now carries only
`packet_type`, `player_id`, `seq`, `mx`, `my`.

**Why**: Nothing in the specification requires lag-compensation for which
the tick hint would be consumed. Keeping an unused field invites
undefined-behaviour in future extensions.

## 7. Explicit `BufferWriter` / `BufferReader` with bounds checking

**Spec**: "The project shall not rely on raw struct memory dumps for network
transmission."

**Extension** (not a deviation — a stronger implementation of the same rule):
all encode/decode go through a bounds-checked, network-byte-order buffer API.
Truncated or over-length packets are rejected rather than causing UB. A
`ll_selftest` CTest target exercises round-trips and truncation failures.

## 8. Headless bot client mode

**Spec**: one desktop client application per player.

**Extension**: the same client binary accepts a `--bot=circle|sine|zigzag|random`
flag that drives scripted input generation, and a `--headless` flag that
skips window creation. This is the only practical way to run the
E_stress_8 condition reproducibly.

## 9. Debug HUD and authoritative "ghost" overlay

**Spec**: no mention of on-screen diagnostics.

**Extension**: F1 toggles a HUD showing the mode, pid, snapshot count, bytes
received, inputs sent, measured input-to-visible delay, interpolation buffer
depth, and reconciliation error. F2 toggles a translucent "ghost" marker
drawn at the authoritative local-player position, making prediction
corrections visible to a demo audience. F3 toggles prediction and F4
toggles interpolation at runtime for live demonstrations.

## 10. Secondary per-input CSV log

**Spec**: client CSV has periodic columns only.

**Extension**: an additional `client_*_inputs_*.csv` file records
per-input rows (`wall_time_ms, player_id, input_seq, ack_seq, rtt_ms,
reconciliation_error_px, perceived_delay_ms, mode`). This is the raw data
the analysis script uses to compute delay distributions; the periodic log
still conforms exactly to the spec's column list.

## 11. Two delay metrics: ack latency and perceived delay

**Spec**: "input-to-visible delay" without a measurement methodology.

**Clarification**: the implementation distinguishes two quantities:
- `rtt_ms` — time from input send to authoritative ack
  (`last_processed_input_seq >= seq`). Identical semantics in both modes;
  measures wire RTT.
- `perceived_delay_ms` — time from input to visible effect on the *local*
  user's screen. In optimized+prediction this is 0 (prediction responds
  on the current frame); in baseline it equals `rtt_ms` (the local player
  only moves when authoritative state arrives).

Both are logged; both are plotted. The perceived delay is the headline
figure because it captures the user-visible benefit of prediction,
which is the central claim in the research objective.
