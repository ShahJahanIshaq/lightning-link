# Lightning Link Final Project Specification

## Project Title
**Lightning Link: A Deterministic Low-Latency UDP Networking Prototype with Client-Side Prediction for a 2D Multiplayer Game**

## Document Purpose
This document is the final, implementation-ready specification for Lightning Link. It defines one fixed project scope, one fixed architecture, one fixed protocol direction, one fixed gameplay model, one fixed repository structure, and one fixed evaluation plan. It removes optional implementation paths and is intended to be precise enough for direct execution by a software engineer or an LLM-based coding agent. The project is positioned as a small, research-oriented multiplayer networking prototype rather than a full commercial game, consistent with the original project framing and constraints. [file:50]

## Project Definition
Lightning Link is a C++20 client-server multiplayer prototype that demonstrates how a lightweight authoritative server, UDP state synchronization, compact binary serialization, client-side prediction, and entity interpolation improve real-time responsiveness in a 2D multiplayer environment under constrained network conditions. [file:50]

The project shall produce a working top-down 2D multiplayer prototype with one server process and multiple client processes. Each client shall capture local input, predict its own movement locally, receive authoritative state from the server, interpolate the motion of remote entities, and render the shared arena. The server shall remain the single authoritative owner of world state and shall broadcast state snapshots at a fixed rate. [file:50]

This project is not a general-purpose engine, not a production-ready networking framework, and not a feature-rich game. It is a deterministic, bounded-scope experimental prototype for evaluating networking behavior in real-time interaction. [file:50]

## Core Research Objective
The project shall answer the following question through implementation and evaluation:

**How much improvement in responsiveness, bandwidth efficiency, and motion smoothness is achieved by replacing a simple baseline transport model with a UDP-based authoritative networking architecture using binary serialization, client-side prediction, and interpolation?** [file:50]

## Final Scope Lock
The implemented system shall include all of the following and nothing beyond them unless explicitly listed as documentation-only future work:

- One authoritative game server.
- One desktop client application.
- A top-down 2D rectangular arena.
- Support for 2 to 8 simultaneous players in one session.
- Four-direction player movement.
- Fixed-timestep simulation.
- A baseline networking mode for comparison.
- An optimized UDP-based networking mode.
- Binary serialization in the optimized mode.
- Client-side prediction for the local player.
- Entity interpolation for remote players.
- Logging and metrics capture.
- Controlled evaluation under latency and packet-loss conditions. [file:50]

The implementation shall not include matchmaking, authentication, persistent storage, inventories, combat systems, projectiles, large maps, anti-cheat, delta compression, rollback networking, custom reliable UDP retransmission, or internet deployment infrastructure. These are explicitly excluded from the implementation. [file:50]

## Fixed Technical Stack
The project shall use the following technology stack:

| Layer | Fixed Choice |
|---|---|
| Programming language | C++20 |
| Networking API | BSD sockets |
| Graphics and input | SFML 3.0 |
| Build system | CMake |
| Concurrency | `std::thread`, `std::mutex`, `std::condition_variable` |
| Logging format | CSV |
| Server transport for optimized mode | UDP |
| Baseline comparison transport | TCP |
| Serialization in optimized mode | Explicit manual byte-buffer serialization |
| Operating assumption | Desktop development on a standard PC/laptop |

The project shall not use external networking middleware, game engines, RPC frameworks, or serialization frameworks. The networking layer must be implemented directly using BSD sockets and shared protocol code. [file:50]

## Fixed Gameplay Specification
The game world shall be a single rectangular top-down arena with a plain background and no obstacles. The purpose of the environment is to expose network behavior clearly rather than to provide content complexity. [file:50]

### Player Capabilities
Each player entity shall support exactly the following:
- Move up.
- Move down.
- Move left.
- Move right.
- Stop when no movement input is active.

There shall be no combat, no inventory, no items, no score system, no AI entities, and no map interactions. Each entity exists only to demonstrate multiplayer state synchronization and latency effects. [file:50]

### Player Representation
Each player shall be represented visually as a colored circle or square with:
- unique player ID,
- current position,
- current velocity,
- assigned display color.

Each client shall render the local player and all remote players simultaneously. [file:50]

## Fixed High-Level Architecture
The system architecture shall consist of three logical layers:

1. **Client layer** — multiple client processes.
2. **Server layer** — one authoritative simulation server.
3. **Shared protocol layer** — common packet definitions, constants, and serialization logic. [file:50]

### Client Responsibilities
Each client shall perform the following functions:
- capture keyboard input each frame,
- convert input into a movement command packet,
- apply local prediction immediately to the local entity,
- send input packets to the server,
- receive server snapshots asynchronously,
- interpolate remote entity motion using a small snapshot buffer,
- render the current local and remote states,
- log client-side metrics. [file:50]

### Server Responsibilities
The server shall perform the following functions:
- accept player session registration,
- assign player IDs,
- receive input packets from clients,
- update the authoritative world state on a fixed simulation tick,
- maintain the authoritative state of all connected players,
- broadcast snapshots to all connected clients at a fixed snapshot rate,
- log server-side metrics including send rate and bytes sent. [file:50]

### Shared Protocol Responsibilities
The shared protocol module shall define:
- packet IDs,
- fixed constants,
- byte-order rules,
- message encoding and decoding,
- common numeric types for state representation. [file:50]

## Fixed Runtime Architecture
The runtime model shall use exactly two threads per client:

- **Main thread**: input capture, local simulation application, interpolation update, and rendering.
- **Network receive thread**: UDP or TCP receive loop, packet parsing, and queueing of received state data. [file:50]

The client shall use one thread-safe queue for transferring received snapshots from the network thread to the main thread. The project shall not use thread pools, ECS systems, async runtimes, or more than one background networking thread. [file:50]

The server shall run in one main loop thread. It may use blocking or timeout-based socket handling, but it shall remain logically single-threaded for simulation, packet handling, and snapshot broadcasting. This is a deliberate simplification for determinism and implementation clarity. [file:50]

## Fixed Networking Design
The project shall implement exactly two networking modes.

### Mode 1: Baseline Mode
The baseline mode shall use:
- TCP,
- text-based message serialization,
- no client-side prediction,
- no interpolation,
- direct application of received authoritative movement updates to rendering. [file:50]

The purpose of this mode is to provide a simple comparison reference for responsiveness and bandwidth cost. It exists only for evaluation and shall not be treated as the primary final design. [file:50]

### Mode 2: Lightning Link Optimized Mode
The optimized mode shall use:
- UDP,
- explicit binary message packing into byte buffers,
- sequence-numbered input packets,
- fixed-rate server snapshots,
- client-side prediction for the local player,
- interpolation for remote players. [file:50]

This mode is the primary deliverable of the project. [file:50]

## Fixed Simulation Model
The simulation shall use a fixed timestep of **60 Hz** on the server. [file:50]

### Timing Constants
The following constants are fixed:

- Server simulation tick rate: **60 ticks per second**.
- Client input send rate: **60 packets per second** in optimized mode.
- Server snapshot broadcast rate: **20 snapshots per second**.
- Remote interpolation delay buffer: **100 ms**.
- Player movement speed: **240 world units per second**.
- Arena size: **1280 × 720 world units**.

The client render loop may run at the display frame rate, but simulation decisions and prediction calculations shall be based on elapsed time consistent with the fixed-timestep model. [file:50]

## Fixed Protocol Specification
The protocol shall use explicit message IDs and fixed field ordering.

### Packet Types
The system shall implement the following packet types only:

1. `JOIN_REQUEST`
2. `JOIN_ACCEPT`
3. `INPUT_COMMAND`
4. `WORLD_SNAPSHOT`
5. `DISCONNECT_NOTICE`

No other packet types shall be implemented in the final system. [file:50]

### Packet Encoding Rules
- All packets shall begin with a 1-byte packet type field.
- All integer fields shall use fixed-width integer types.
- All numeric fields shall be serialized explicitly into byte buffers in network byte order where applicable.
- Floating-point values may be transmitted as 32-bit IEEE floats.
- The project shall not rely on raw struct memory dumps for network transmission. [file:50]

### JOIN_REQUEST
Fields:
- packet type: `uint8`
- client version: `uint16`

### JOIN_ACCEPT
Fields:
- packet type: `uint8`
- assigned player ID: `uint16`
- arena width: `float32`
- arena height: `float32`

### INPUT_COMMAND
Fields:
- packet type: `uint8`
- player ID: `uint16`
- input sequence: `uint32`
- tick hint: `uint32`
- move X: `int8` where value is -1, 0, or 1
- move Y: `int8` where value is -1, 0, or 1

### PLAYER_STATE record inside WORLD_SNAPSHOT
Fields:
- player ID: `uint16`
- position X: `float32`
- position Y: `float32`
- velocity X: `float32`
- velocity Y: `float32`

### WORLD_SNAPSHOT
Fields:
- packet type: `uint8`
- server tick: `uint32`
- player count: `uint16`
- repeated `PLAYER_STATE` records

### DISCONNECT_NOTICE
Fields:
- packet type: `uint8`
- player ID: `uint16`

## Fixed World Update Rules
The server shall maintain the authoritative position and velocity of every connected player. [file:50]

At each server tick:
1. The server reads all queued input commands received since the previous tick.
2. For each player, the server determines the latest valid directional input.
3. The server computes the player velocity from the direction vector and fixed movement speed.
4. The server updates positions using the fixed timestep.
5. The server clamps all player positions to arena bounds.
6. The server increments the tick counter.
7. Every third tick, the server broadcasts a `WORLD_SNAPSHOT` to all connected clients, yielding 20 snapshots per second. [file:50]

The server shall not simulate acceleration, friction, collision, projectiles, or complex physics. Movement shall be deterministic and purely kinematic. [file:50]

## Fixed Client Prediction Rules
The client shall immediately apply the local input command to the local player representation before server confirmation. [file:50]

Prediction shall follow these rules:
- prediction applies only to the local player,
- predicted movement uses the same movement speed and timestep assumptions as the server,
- each input packet increments a local input sequence number,
- the client stores a bounded history of the most recent 120 local input commands,
- when a new authoritative snapshot arrives, the client replaces the local player state with the authoritative state from the snapshot and then reapplies all stored local inputs with sequence number greater than the last acknowledged snapshot tick approximation.

For this implementation, server acknowledgements shall be approximated by snapshot tick ordering rather than a dedicated ack field. This simplification is fixed for the final system. The implementation shall not introduce rollback, delta reconciliation, or lag compensation beyond this design. [file:50]

## Fixed Interpolation Rules
Interpolation shall apply only to remote players. [file:50]

The client shall maintain a buffer of recent snapshots for remote entities and render them 100 ms behind the latest received time. For each remote player:
1. Find the two snapshots surrounding the target interpolation time.
2. Linearly interpolate position between the earlier and later snapshot.
3. If only one usable snapshot exists, render that latest known state directly.
4. If the entity is absent from a newer snapshot, retain the latest valid state until timeout or disconnect notice.

The client shall not extrapolate remote players. This is a fixed decision to avoid speculative motion and keep behavior simple and defensible. [file:50]

## Fixed Logging Specification
The project shall produce CSV logs for both server and clients.

### Server Log Columns
- wall_time_ms
- server_tick
- connected_players
- bytes_sent_total
- snapshots_sent_total
- input_packets_received_total

### Client Log Columns
- wall_time_ms
- player_id
- latest_snapshot_tick
- bytes_received_total
- input_packets_sent_total
- estimated_input_to_visible_delay_ms
- interpolation_buffer_entries

Each experimental run shall produce a uniquely named log file containing the test scenario label in the filename. [file:50]

## Fixed Evaluation Metrics
The project shall evaluate the following metrics:

1. **Bandwidth per client** in kilobits per second.
2. **Input-to-visible delay** in milliseconds.
3. **Remote motion jitter** measured as frame-to-frame position variance for remote entities.
4. **Snapshot delivery stability** as observed snapshot arrival consistency.
5. **System behavior under packet loss** described through comparative observations and basic counters. [file:50]

The project shall not attempt advanced QoE models, perceptual questionnaires, or subjective user studies. [file:50]

## Fixed Test Conditions
The system shall be tested under exactly the following conditions:

1. **Condition A: Local clean network**
   - Added latency: 0 ms
   - Packet loss: 0%

2. **Condition B: Moderate latency**
   - Added latency: 100 ms round-trip equivalent
   - Packet loss: 0%

3. **Condition C: High latency**
   - Added latency: 200 ms round-trip equivalent
   - Packet loss: 0%

4. **Condition D: Packet loss**
   - Added latency: 100 ms round-trip equivalent
   - Packet loss: 3%

5. **Condition E: Multi-client stress**
   - Added latency: 100 ms round-trip equivalent
   - Packet loss: 0%
   - Client count: 8

Each condition shall be run in both baseline mode and optimized mode. [file:50]

## Fixed Deliverables
The implementation shall produce exactly the following deliverables:

1. A C++20 server application.
2. A C++20 client application.
3. A CMake-based build system.
4. Source code organized according to the repository structure defined below.
5. CSV logs from the required experiments.
6. A results summary file or report section generated from those logs.
7. A short demonstration-ready playable prototype. [file:50]

## Fixed Repository Structure
The repository shall follow this structure exactly:

```text
lightning-link/
├── CMakeLists.txt
├── README.md
├── common/
│   ├── constants.hpp
│   ├── protocol.hpp
│   ├── serialization.hpp
│   ├── serialization.cpp
│   ├── types.hpp
│   └── math_utils.hpp
├── server/
│   ├── main.cpp
│   ├── server.hpp
│   ├── server.cpp
│   ├── world_state.hpp
│   ├── world_state.cpp
│   ├── session_manager.hpp
│   ├── session_manager.cpp
│   ├── udp_snapshot_broadcaster.hpp
│   └── udp_snapshot_broadcaster.cpp
├── client/
│   ├── main.cpp
│   ├── client.hpp
│   ├── client.cpp
│   ├── renderer.hpp
│   ├── renderer.cpp
│   ├── input.hpp
│   ├── input.cpp
│   ├── prediction.hpp
│   ├── prediction.cpp
│   ├── interpolation.hpp
│   ├── interpolation.cpp
│   ├── network_receiver.hpp
│   ├── network_receiver.cpp
│   └── snapshot_queue.hpp
├── logs/
├── results/
└── docs/
    └── project_specification.md
```

## Fixed Module Responsibilities

### `common/constants.hpp`
Defines all simulation, timing, arena, and protocol constants.

### `common/types.hpp`
Defines common entity and packet-related data structures used in memory.

### `common/protocol.hpp`
Defines packet IDs, packet field ordering comments, and protocol-related enums.

### `common/serialization.*`
Implements all encode/decode functions for packet payloads.

### `server/world_state.*`
Stores and updates authoritative positions and velocities for all players.

### `server/session_manager.*`
Tracks connected clients, player IDs, and endpoint mappings.

### `server/server.*`
Coordinates socket handling, tick loop, input processing, and snapshot broadcasts.

### `client/prediction.*`
Handles local prediction history and reconciliation against snapshots.

### `client/interpolation.*`
Maintains remote snapshot buffers and computes interpolated render positions.

### `client/network_receiver.*`
Runs the background receive thread and pushes parsed state packets into the queue.

### `client/renderer.*`
Draws the arena, local player, and remote players in SFML.

## Fixed Build Requirements
The codebase shall compile using CMake and produce two binaries:
- `lightning_link_server`
- `lightning_link_client`

The build shall require only:
- a C++20 compiler,
- SFML 3.0,
- standard system socket libraries. [file:50]

## Fixed README Requirements
The repository README shall include:
- project overview,
- build instructions,
- run instructions for server and client,
- explanation of baseline mode and optimized mode,
- explanation of experiment conditions,
- log file output locations,
- known limitations. [file:50]

## Fixed Success Criteria
The project shall be considered complete only if all of the following are true:

- The server starts and accepts client registration.
- At least two clients can connect simultaneously.
- All connected players can move in the same shared arena.
- Baseline mode works using TCP and text-based messages.
- Optimized mode works using UDP and binary packets.
- Local movement in optimized mode uses prediction.
- Remote movement in optimized mode uses interpolation.
- Logs are generated for every required experiment.
- Results can be summarized for all five test conditions.
- The codebase matches the specified architecture and repository structure. [file:50]

## Fixed Non-Goals
The following shall not be implemented and shall be documented only as future work:
- rollback netcode,
- custom reliability and retransmission over UDP,
- projectile synchronization,
- WAN hosting,
- matchmaking,
- database persistence,
- anti-cheat,
- ECS conversion,
- engine abstraction layers,
- asset pipelines,
- advanced UI systems. [file:50]

## Implementation Order
The code shall be built in the following order:

1. Common constants and protocol definitions.
2. Server world state and session manager.
3. TCP baseline communication path.
4. SFML client rendering and local input.
5. UDP optimized packet path.
6. Binary serialization and parsing.
7. Local prediction logic.
8. Remote interpolation logic.
9. Logging.
10. Experiment execution and results generation. [file:50]


## Final Reporting and Results Requirements
The project implementation shall not stop at a working prototype. It shall also generate the full set of quantitative and visual evidence required for the final report, poster, and presentation. [file:50]

### Required Outputs
The completed project shall produce all of the following:
- A baseline-versus-optimized comparison of networking performance.
- Numerical results for every required test condition.
- Processed metrics derived from experiment logs.
- At least three publication-quality charts or figures.
- Written observations explaining measured improvements and remaining limitations.
- A concise conclusions section suitable for direct reuse in the final report. [file:50]

### Required Performance Metrics
The implementation shall measure and report the following metrics:
- Input-to-visible delay in milliseconds.
- Average bandwidth usage per client.
- Remote motion smoothness or jitter.
- Snapshot delivery stability.
- Behavior under packet loss.
- Behavior under increased client count. [file:50]

### Required Figures and Tables
The final results package shall include at minimum:
1. A chart comparing average input-to-visible delay between baseline TCP mode and optimized UDP mode.
2. A chart comparing bandwidth usage between baseline and optimized modes.
3. A chart showing responsiveness or motion smoothness under increasing latency and packet loss.
4. A summary table covering all test conditions and observed results. [file:50]

### Required Observations
The final write-up shall include explicit observations on:
- How client-side prediction affects perceived responsiveness.
- How interpolation affects the appearance of remote player movement.
- How binary serialization changes packet size and bandwidth use.
- How latency and packet loss affect motion quality.
- Which components contribute the most significant improvement.
- Which weaknesses remain in the final system. [file:50]

### Required Final Interpretation
The results section shall interpret the measured data rather than present raw numbers only. It shall explicitly identify:
- The strongest improvement introduced by the optimized design.
- The trade-offs introduced by the optimized design.
- The principal technical limitation of the final system.
- The practical conclusion about whether the optimized architecture is superior to the baseline for the defined use case. [file:50]

### Reporting Constraints
- All reported results shall come from the implemented experiment pipeline and generated logs.
- Charts and tables shall be reproducible from saved experimental data.
- The final report shall distinguish clearly between measured results and interpretation.
- Once implementation begins, no section of the final report shall use fabricated values. [file:50]

## Final Instruction for the Implementing Agent
Implement exactly the system described in this document. Do not widen the scope, add optional game features, replace the protocol model, or substitute frameworks. Preserve the authoritative client-server design, the two networking modes, the fixed-timestep movement model, the fixed packet types, the logging scheme, and the exact repository structure. The objective is to produce a compact, deterministic, evaluation-ready prototype aligned with this specification. [file:50]
