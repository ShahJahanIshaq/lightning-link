# Lightning Link: Architecture and Optimization Decomposition

This document is the visual companion to the evaluation chapter of the report.
It traces how input travels through the system, where each optimization lives,
and which figure in the results set is the evidence for it.

## 1. Component overview

```mermaid
flowchart LR
  subgraph clientSide [Client process]
    input[Keyboard or Bot]
    pred[Predictor]
    interp[Interpolator]
    queue[SnapshotQueue]
    rxThread[NetworkReceiver thread]
    render[Renderer + HUD]
    logs[CSV logger]
  end
  subgraph transport [Transport]
    lagSock["LaggedSocket<br/>(latency, jitter, loss)"]
  end
  subgraph serverSide [Server process]
    listener["UDP listen loop<br/>(handle_inbound_udp)"]
    session[SessionManager]
    world[WorldState]
    bcast[UdpSnapshotBroadcaster]
    slog[CSV logger]
  end

  input --> pred
  pred -->|"INPUT_COMMAND"| lagSock
  lagSock --> listener --> session --> world --> bcast
  bcast -->|"WORLD_SNAPSHOT"| lagSock
  lagSock --> rxThread --> queue
  queue --> pred
  queue --> interp
  pred --> render
  interp --> render

  pred --> logs
  render --> logs
  world --> slog
  bcast --> slog
```

The baseline (TCP + text) path has the same shape except the transport is a
stream socket, packets are newline-delimited ASCII, there is no predictor or
interpolator, and the conditioner is a small line-level software queue that
lives inside the baseline client itself (`Conditioner` in
[client/tcp_baseline_client.cpp](../client/tcp_baseline_client.cpp)).

## 2. Input round-trip timeline (optimized path)

```mermaid
sequenceDiagram
  autonumber
  participant U as User / Bot
  participant P as Predictor
  participant L as LaggedSocket (c)
  participant S as Server
  participant L2 as LaggedSocket (s)
  participant I as Interpolator
  participant R as Renderer

  U->>P: key state (60 Hz)
  P->>P: record_and_step(seq, dt)<br/>advance local_pos
  P->>R: draw at predicted pos (this frame)
  P->>L: INPUT_COMMAND(seq, mx, my)
  L->>S: after half-RTT + jitter, possibly dropped
  S->>S: SessionManager.verify_input<br/>WorldState.apply_input<br/>last_processed_input_seq = seq
  S->>L2: WORLD_SNAPSHOT (every 3rd tick = 20 Hz)
  L2->>I: snapshot with last_processed_input_seq
  I->>P: reconcile: snap authoritative, replay unacked
  I->>R: render remote entities with 100 ms delay
```

The `reconcile` step is what decouples perceived delay from wire RTT. Because
the predictor already advanced `local_pos` in step (3), the frame rendered at
step (3) is the one the user perceives - the ack arriving much later only
corrects drift, it does not delay the visible response.

## 3. Where each optimization lives

```mermaid
flowchart TB
  subgraph transport [Transport choices]
    tcp[TCP + newline text]
    udp[UDP + binary]
  end
  subgraph latencyHiding [User-perceived latency]
    pred[Client-side prediction + reconciliation]
  end
  subgraph smoothness [Remote motion smoothness]
    interp[Entity interpolation buffer]
  end
  subgraph session [Session robustness]
    sess[Endpoint-bound sessions]
    live[Liveness timeout]
  end
  subgraph protocol [Protocol economy]
    bin[Explicit binary serialization]
    ack[last_processed_input_seq]
  end

  udp --> bin
  bin --> fig7[fig7_bytes_per_snapshot]
  bin --> fig2[fig2_bandwidth]
  bin --> fig8[fig8_bandwidth_scaling]
  pred --> fig1[fig1_perceived_delay]
  pred --> fig4[fig4_perceived_vs_latency]
  pred --> fig5[fig5_isolation_decomp]
  interp --> fig3[fig3_stability]
  interp --> fig10[fig10_snapshot_jitter_cv]
  sess --> fig9[fig9_loss_delay_tail]
  live --> fig9
  ack --> fig1b[fig1b_ack_latency]
  ack --> fig6[fig6_ack_latency_cdf]
```

A claim in the report that "binary serialization gives X% bandwidth savings"
must cite fig2 and fig7. A claim that "client-side prediction hides the
user-visible effect of 100 ms added RTT" must cite fig1, fig4, and fig5. If a
figure is referenced and no arrow in the diagram leads to it, the claim is
not supported by the evaluation.

## 4. Evaluation pipeline

```mermaid
flowchart LR
  A["run_experiments.sh<br/>(5 cond x 2 mode x 3 reps)"] --> B1[experiments/]
  C[run_isolation.sh] --> B2[experiments_isolation/]
  D[run_loss_sweep.sh] --> B3[experiments_loss/]
  E[run_scale_sweep.sh] --> B4[experiments_scale/]
  B1 -->|"analyze.py --mode main"| R1["results/<br/>fig1, fig1b, fig2, fig3, fig4, fig6"]
  B2 -->|"analyze.py --mode isolation"| R2["results_isolation/<br/>fig5"]
  B3 -->|"analyze.py --mode loss"| R3["results_loss/<br/>fig9, fig10"]
  B4 -->|"analyze.py --mode scale"| R4["results_scale/<br/>fig7, fig8"]
```

Each orchestration script is self-contained: you can run only the scripts that
produce the figures you cite. Every script reuses the same built binaries and
the same CSV schemas.

## 5. Source-to-figure index

| Optimization             | Source                                                                                                           | Evidence figure(s)                  |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------- | ----------------------------------- |
| UDP + binary transport   | [common/lagged_socket.cpp](../common/lagged_socket.cpp), [common/serialization.cpp](../common/serialization.cpp) | fig2, fig7, fig8                    |
| Client-side prediction   | [client/prediction.cpp](../client/prediction.cpp)                                                                | fig0, fig1, fig4, fig5              |
| Entity interpolation     | [client/interpolation.cpp](../client/interpolation.cpp)                                                          | fig3, fig3b, fig10                  |
| Ack-based reconciliation | `last_processed_input_seq` in [common/serialization.cpp](../common/serialization.cpp)                            | fig0, fig1b, fig5 (noPred vs full)  |
| Endpoint-bound sessions  | [server/session_manager.cpp](../server/session_manager.cpp)                                                      | fig9 (no spurious stalls under loss)|
| Liveness timeout         | [server/session_manager.cpp](../server/session_manager.cpp) `reap_timeouts`                                      | fig9                                |
| Fixed-timestep tick      | [server/world_state.cpp](../server/world_state.cpp) `step_tick`                                                  | fig3, fig3b                         |

When presenting results in the report, group the text by the rows of this
table rather than by figure number - the reader leaves with "what did each
design decision buy us?" which is the question the evaluation is designed to
answer.
