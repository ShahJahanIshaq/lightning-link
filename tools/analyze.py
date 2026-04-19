#!/usr/bin/env python3
"""
Lightning Link analysis pipeline.

Consumes the CSVs produced by the orchestration scripts under
`tools/run_*.sh` and emits publication-ready figures plus tabular
summaries under `results*/`.

Supports four operating modes (selected via --mode):

  main       Reads `experiments/`, produces the headline figures
             fig1 (perceived delay), fig1b (ack latency), fig2 (bandwidth),
             fig3 (stability), fig4 (perceived vs latency), fig6 (ack latency
             CDF). Results go to `results/`.

  isolation  Reads `experiments_isolation/`, produces fig5_isolation_decomp.png
             which decomposes where the perceived-delay gain comes from.
             Results go to `results_isolation/`.

  loss       Reads `experiments_loss/`, produces fig9_loss_delay_tail.png and
             fig10_snapshot_jitter_cv.png showing degradation as loss rises.
             Results go to `results_loss/`.

  scale      Reads `experiments_scale/`, produces fig7_bytes_per_snapshot.png
             and fig8_bandwidth_scaling.png. Results go to `results_scale/`.

Metrics:
 * Input-to-visible delay (ms)     - client_*_inputs_*.csv `rtt_ms` column
 * Perceived responsiveness (ms)   - client_*_inputs_*.csv `perceived_delay_ms`
 * Bandwidth per client (kbps)     - (delta bytes_received_total / delta t) * 8 / 1000
 * Snapshot arrival stability      - coefficient of variation of inter-tick arrivals
 * Bytes per snapshot              - server bytes_sent_total / snapshots_sent_total
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


# ---- layout helpers ---------------------------------------------------------

CONDITION_ORDER = [
    "A_clean",
    "B_lat100",
    "C_lat200",
    "D_lat100_loss3",
    "E_stress_8",
]

MODES = ["optimized", "baseline"]

RUN_RE          = re.compile(r"^(?P<cond>[A-Z]_[A-Za-z0-9_]+?)_(?P<mode>optimized|baseline)_rep(?P<rep>\d+)$")
ISOLATION_RE    = re.compile(r"^(?P<variant>baseline|optimized|optimized_raw|optimized_noInterp|optimized_noPred)_rep(?P<rep>\d+)$")
LOSS_RE         = re.compile(r"^loss(?P<loss>\d+)_(?P<mode>optimized|baseline)_rep(?P<rep>\d+)$")
SCALE_RE        = re.compile(r"^N(?P<n>\d+)_(?P<mode>optimized|baseline)_rep(?P<rep>\d+)$")

ISOLATION_ORDER = [
    "baseline",
    "optimized_raw",
    "optimized_noInterp",
    "optimized_noPred",
    "optimized",
]
ISOLATION_LABELS = {
    "baseline":           "baseline\n(TCP+text)",
    "optimized_raw":      "UDP+binary\n(no pred, no interp)",
    "optimized_noInterp": "+ prediction\n(no interp)",
    "optimized_noPred":   "+ interpolation\n(no pred)",
    "optimized":          "full stack\n(pred + interp)",
}


# ---- shared loaders ---------------------------------------------------------

def load_periodic(run_dir: Path) -> pd.DataFrame:
    frames = []
    for p in run_dir.glob("client_*_periodic_*.csv"):
        try:
            df = pd.read_csv(p)
            df["source_file"] = p.name
            frames.append(df)
        except Exception as e:
            print(f"warn: failed to read {p}: {e}", file=sys.stderr)
    return pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()


def load_inputs(run_dir: Path) -> pd.DataFrame:
    frames = []
    for p in run_dir.glob("client_*_inputs_*.csv"):
        try:
            frames.append(pd.read_csv(p))
        except Exception as e:
            print(f"warn: failed to read {p}: {e}", file=sys.stderr)
    return pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()


def load_server(run_dir: Path) -> pd.DataFrame:
    frames = []
    for p in run_dir.glob("server_*.csv"):
        try:
            frames.append(pd.read_csv(p))
        except Exception as e:
            print(f"warn: failed to read {p}: {e}", file=sys.stderr)
    if not frames:
        return pd.DataFrame()
    return pd.concat(frames, ignore_index=True).sort_values("wall_time_ms")


# ---- per-run parsing (main matrix) ------------------------------------------

@dataclass
class RunAggregates:
    condition: str
    mode: str
    rep: int
    mean_delay_ms: float = math.nan
    median_delay_ms: float = math.nan
    p95_delay_ms: float = math.nan
    mean_perceived_ms: float = math.nan
    p95_perceived_ms: float = math.nan
    bandwidth_kbps_mean: float = math.nan
    snapshot_rate_hz: float = math.nan
    snapshot_arrival_cv: float = math.nan
    # Inter-snapshot arrival gap percentiles derived from the per-input CSV:
    # consecutive input rows that share a wall_time_ms were acked by the same
    # snapshot event, so unique wall_time_ms values form the snapshot arrival
    # timeline.
    arrival_gap_p50_ms: float = math.nan
    arrival_gap_p95_ms: float = math.nan
    arrival_gap_p99_ms: float = math.nan
    bytes_per_snapshot: float = math.nan
    clients: int = 0
    total_bytes_sent: int = 0


def discover(exp_dir: Path, regex: re.Pattern) -> List[Path]:
    out = []
    if not exp_dir.exists():
        return out
    for d in sorted(exp_dir.iterdir()):
        if d.is_dir() and regex.match(d.name):
            out.append(d)
    return out


def _aggregate_common(run_dir: Path, agg: RunAggregates) -> None:
    inputs = load_inputs(run_dir)
    if not inputs.empty and "rtt_ms" in inputs.columns:
        start_t = inputs["wall_time_ms"].min()
        body = inputs[inputs["wall_time_ms"] >= start_t + 500]
        if len(body) > 0:
            agg.mean_delay_ms   = float(body["rtt_ms"].mean())
            agg.median_delay_ms = float(body["rtt_ms"].median())
            agg.p95_delay_ms    = float(body["rtt_ms"].quantile(0.95))
            if "perceived_delay_ms" in body.columns:
                agg.mean_perceived_ms = float(body["perceived_delay_ms"].mean())
                agg.p95_perceived_ms  = float(body["perceived_delay_ms"].quantile(0.95))

            # Inter-snapshot arrival gaps. Per client, take unique wall_time_ms
            # values (each corresponds to one snapshot event, because all inputs
            # acked by that snapshot share the logged millisecond) and diff.
            gaps: List[float] = []
            for _, sub in body.groupby("player_id"):
                ts = np.sort(sub["wall_time_ms"].unique())
                if len(ts) >= 2:
                    d = np.diff(ts)
                    # Defensive: drop anomalously large gaps caused by logging
                    # flushes at shutdown.
                    d = d[d <= 1000]
                    gaps.extend(d.tolist())
            if gaps:
                arr = np.array(gaps, dtype=float)
                agg.arrival_gap_p50_ms = float(np.quantile(arr, 0.50))
                agg.arrival_gap_p95_ms = float(np.quantile(arr, 0.95))
                agg.arrival_gap_p99_ms = float(np.quantile(arr, 0.99))

    periodic = load_periodic(run_dir)
    if not periodic.empty:
        agg.clients = periodic["player_id"].nunique()
        bws, arrivals_cv = [], []
        for _, sub in periodic.groupby("player_id"):
            sub = sub.sort_values("wall_time_ms")
            if len(sub) < 2:
                continue
            dt_s = (sub["wall_time_ms"].iloc[-1] - sub["wall_time_ms"].iloc[0]) / 1000.0
            dbytes = sub["bytes_received_total"].iloc[-1] - sub["bytes_received_total"].iloc[0]
            if dt_s > 0:
                bws.append((dbytes * 8.0 / 1000.0) / dt_s)
            if "latest_snapshot_tick" in sub.columns:
                dtick = sub["latest_snapshot_tick"].diff().dropna()
                if len(dtick) > 1 and dtick.mean() > 0:
                    arrivals_cv.append(dtick.std() / dtick.mean())
        if bws:
            agg.bandwidth_kbps_mean = float(np.mean(bws))
        if arrivals_cv:
            agg.snapshot_arrival_cv = float(np.mean(arrivals_cv))

    server = load_server(run_dir)
    if not server.empty and len(server) >= 2:
        first, last = server.iloc[0], server.iloc[-1]
        dur_s = (last["wall_time_ms"] - first["wall_time_ms"]) / 1000.0
        if dur_s > 0:
            n_snaps = last["snapshots_sent_total"] - first["snapshots_sent_total"]
            agg.snapshot_rate_hz = float(n_snaps / dur_s)
            if n_snaps > 0:
                dbytes = last["bytes_sent_total"] - first["bytes_sent_total"]
                agg.bytes_per_snapshot = float(dbytes / n_snaps)
        agg.total_bytes_sent = int(last["bytes_sent_total"])


def aggregate_run(run_dir: Path) -> RunAggregates:
    m = RUN_RE.match(run_dir.name)
    assert m is not None, run_dir.name
    agg = RunAggregates(condition=m["cond"], mode=m["mode"], rep=int(m["rep"]))
    _aggregate_common(run_dir, agg)
    return agg


def aggregate_generic(run_dir: Path, condition: str, mode: str, rep: int) -> RunAggregates:
    agg = RunAggregates(condition=condition, mode=mode, rep=rep)
    _aggregate_common(run_dir, agg)
    return agg


# ---- cross-run summary ------------------------------------------------------

def _agg_stat(vals):
    arr = np.array([v for v in vals if not (isinstance(v, float) and math.isnan(v))])
    if len(arr) == 0:
        return (math.nan, math.nan)
    if len(arr) < 2:
        return (float(arr.mean()), 0.0)
    ci = 1.96 * float(arr.std(ddof=1)) / math.sqrt(len(arr))
    return (float(arr.mean()), ci)


def summarize_main(runs: List[RunAggregates]) -> pd.DataFrame:
    rows = []
    grouped: Dict[Tuple[str, str], List[RunAggregates]] = defaultdict(list)
    for r in runs:
        grouped[(r.condition, r.mode)].append(r)

    for cond in CONDITION_ORDER:
        for mode in MODES:
            reps = grouped.get((cond, mode), [])
            if not reps:
                continue
            delay_mean, delay_ci = _agg_stat([r.mean_delay_ms for r in reps])
            perceived_mean, perceived_ci = _agg_stat([r.mean_perceived_ms for r in reps])
            bw_mean, bw_ci       = _agg_stat([r.bandwidth_kbps_mean for r in reps])
            sr_mean, sr_ci       = _agg_stat([r.snapshot_rate_hz for r in reps])
            cv_mean, cv_ci       = _agg_stat([r.snapshot_arrival_cv for r in reps])
            bps_mean, bps_ci     = _agg_stat([r.bytes_per_snapshot for r in reps])
            gap50_m, gap50_ci    = _agg_stat([r.arrival_gap_p50_ms for r in reps])
            gap95_m, gap95_ci    = _agg_stat([r.arrival_gap_p95_ms for r in reps])
            gap99_m, gap99_ci    = _agg_stat([r.arrival_gap_p99_ms for r in reps])
            rows.append({
                "condition": cond, "mode": mode, "reps": len(reps),
                "clients": reps[0].clients,
                "mean_delay_ms": delay_mean, "delay_ci95_ms": delay_ci,
                "mean_perceived_ms": perceived_mean, "perceived_ci95_ms": perceived_ci,
                "bandwidth_kbps": bw_mean, "bandwidth_ci95": bw_ci,
                "snapshot_rate_hz": sr_mean, "snapshot_rate_ci95": sr_ci,
                "snapshot_arrival_cv": cv_mean, "snapshot_arrival_cv_ci95": cv_ci,
                "arrival_gap_p50_ms": gap50_m, "arrival_gap_p50_ci95": gap50_ci,
                "arrival_gap_p95_ms": gap95_m, "arrival_gap_p95_ci95": gap95_ci,
                "arrival_gap_p99_ms": gap99_m, "arrival_gap_p99_ci95": gap99_ci,
                "bytes_per_snapshot": bps_mean, "bytes_per_snapshot_ci95": bps_ci,
            })
    return pd.DataFrame(rows)


# ---- main-mode plotting -----------------------------------------------------

def _bar_pair(ax, df, metric, ci_col, ylabel, title):
    conds = [c for c in CONDITION_ORDER if c in df["condition"].values]
    x = np.arange(len(conds))
    width = 0.35

    def pluck(mode):
        means, cis = [], []
        for c in conds:
            row = df[(df.condition == c) & (df["mode"] == mode)]
            if row.empty:
                means.append(0); cis.append(0)
            else:
                means.append(float(row[metric].iloc[0]))
                cis.append(float(row[ci_col].iloc[0]))
        return means, cis

    opt_m, opt_ci = pluck("optimized")
    base_m, base_ci = pluck("baseline")
    ax.bar(x - width/2, base_m, width, yerr=base_ci, capsize=3, label="baseline (TCP+text)")
    ax.bar(x + width/2, opt_m,  width, yerr=opt_ci,  capsize=3, label="optimized (UDP+binary+pred+interp)")
    ax.set_xticks(x)
    ax.set_xticklabels(conds, rotation=15, ha="right")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.legend()
    ax.grid(axis="y", alpha=0.3)


def plot_main(df: pd.DataFrame, runs: List[RunAggregates],
              exp_dir: Path, results_dir: Path) -> List[str]:
    results_dir.mkdir(parents=True, exist_ok=True)
    written: List[str] = []

    # Figure 0: wire vs perceived contrast - the single chart that tells the
    # whole story. Two grouped bars per (condition, mode): wire ack latency
    # (what the network takes) and perceived delay (what the user feels).
    conds = [c for c in CONDITION_ORDER if c in df["condition"].values]
    fig, ax = plt.subplots(figsize=(11, 5.8))
    x = np.arange(len(conds))
    group = 0.42
    inner = 0.19

    def pluck(mode, col, ci):
        vals, cis = [], []
        for c in conds:
            row = df[(df.condition == c) & (df["mode"] == mode)]
            if row.empty:
                vals.append(0); cis.append(0)
            else:
                vals.append(float(row[col].iloc[0]))
                cis.append(float(row[ci].iloc[0]))
        return vals, cis

    base_wire, base_wire_ci = pluck("baseline", "mean_delay_ms", "delay_ci95_ms")
    base_per,  base_per_ci  = pluck("baseline", "mean_perceived_ms", "perceived_ci95_ms")
    opt_wire,  opt_wire_ci  = pluck("optimized", "mean_delay_ms", "delay_ci95_ms")
    opt_per,   opt_per_ci   = pluck("optimized", "mean_perceived_ms", "perceived_ci95_ms")

    ax.bar(x - group + 0*inner, base_wire, inner, yerr=base_wire_ci, capsize=3,
           label="baseline wire RTT (ack latency)")
    ax.bar(x - group + 1*inner, base_per,  inner, yerr=base_per_ci,  capsize=3,
           label="baseline perceived delay")
    ax.bar(x + 0*inner,         opt_wire,  inner, yerr=opt_wire_ci,  capsize=3,
           label="optimized wire RTT (ack latency)")
    ax.bar(x + 1*inner,         opt_per,   inner, yerr=opt_per_ci,   capsize=3,
           label="optimized perceived delay")

    ax.set_xticks(x)
    ax.set_xticklabels(conds, rotation=15, ha="right")
    ax.set_ylabel("Milliseconds")
    ax.set_title("Wire RTT vs perceived delay: the network is equal, the user experience is not")
    ax.legend(fontsize=8, loc="upper left")
    ax.grid(axis="y", alpha=0.3)
    plt.tight_layout(); fig.savefig(results_dir / "fig0_wire_vs_perceived.png", dpi=160); plt.close(fig)
    written.append("fig0_wire_vs_perceived.png")

    # Figure 1: perceived input-to-visible delay (headline).
    fig, ax = plt.subplots(figsize=(8.5, 5))
    _bar_pair(ax, df, "mean_perceived_ms", "perceived_ci95_ms",
              "Perceived input-to-visible delay (ms)",
              "Perceived responsiveness by condition and mode")
    plt.tight_layout(); fig.savefig(results_dir / "fig1_perceived_delay.png", dpi=160); plt.close(fig)
    written.append("fig1_perceived_delay.png")

    # Figure 1b: authoritative ack latency (wire RTT).
    fig, ax = plt.subplots(figsize=(8.5, 5))
    _bar_pair(ax, df, "mean_delay_ms", "delay_ci95_ms",
              "Authoritative ack latency (ms)",
              "Wire RTT (time from input to authoritative confirmation)")
    plt.tight_layout(); fig.savefig(results_dir / "fig1b_ack_latency.png", dpi=160); plt.close(fig)
    written.append("fig1b_ack_latency.png")

    # Figure 2: bandwidth per client.
    fig, ax = plt.subplots(figsize=(8.5, 5))
    _bar_pair(ax, df, "bandwidth_kbps", "bandwidth_ci95",
              "Bandwidth per client (kbps)",
              "Average downlink bandwidth by condition and mode")
    plt.tight_layout(); fig.savefig(results_dir / "fig2_bandwidth.png", dpi=160); plt.close(fig)
    written.append("fig2_bandwidth.png")

    # Figure 3: snapshot arrival stability (lower CV = steadier).
    fig, ax = plt.subplots(figsize=(8.5, 5))
    _bar_pair(ax, df, "snapshot_arrival_cv", "snapshot_arrival_cv_ci95",
              "Snapshot arrival CV (lower = steadier)",
              "Snapshot delivery stability by condition and mode")
    plt.tight_layout(); fig.savefig(results_dir / "fig3_stability.png", dpi=160); plt.close(fig)
    written.append("fig3_stability.png")

    # Figure 3b: inter-snapshot arrival gap percentiles. Gaps are what the
    # user actually perceives as stutter; CV averages them away. Grouped lines
    # for p50/p95/p99 per mode across conditions.
    fig, ax = plt.subplots(figsize=(10, 5.8))
    xr = np.arange(len(conds))
    width = 0.13
    p50_b, p50_b_ci = pluck("baseline",  "arrival_gap_p50_ms", "arrival_gap_p50_ci95")
    p95_b, p95_b_ci = pluck("baseline",  "arrival_gap_p95_ms", "arrival_gap_p95_ci95")
    p99_b, p99_b_ci = pluck("baseline",  "arrival_gap_p99_ms", "arrival_gap_p99_ci95")
    p50_o, p50_o_ci = pluck("optimized", "arrival_gap_p50_ms", "arrival_gap_p50_ci95")
    p95_o, p95_o_ci = pluck("optimized", "arrival_gap_p95_ms", "arrival_gap_p95_ci95")
    p99_o, p99_o_ci = pluck("optimized", "arrival_gap_p99_ms", "arrival_gap_p99_ci95")
    ax.bar(xr - 3*width, p50_b, width, yerr=p50_b_ci, capsize=2, label="baseline p50")
    ax.bar(xr - 2*width, p95_b, width, yerr=p95_b_ci, capsize=2, label="baseline p95")
    ax.bar(xr - 1*width, p99_b, width, yerr=p99_b_ci, capsize=2, label="baseline p99")
    ax.bar(xr + 0*width, p50_o, width, yerr=p50_o_ci, capsize=2, label="optimized p50")
    ax.bar(xr + 1*width, p95_o, width, yerr=p95_o_ci, capsize=2, label="optimized p95")
    ax.bar(xr + 2*width, p99_o, width, yerr=p99_o_ci, capsize=2, label="optimized p99")
    ax.axhline(50.0, linestyle="--", alpha=0.4,
               label="nominal 20 Hz gap (50 ms)")
    ax.set_xticks(xr)
    ax.set_xticklabels(conds, rotation=15, ha="right")
    ax.set_ylabel("Inter-snapshot arrival gap (ms)")
    ax.set_title("Snapshot arrival gap percentiles: how often and how badly updates stutter")
    ax.legend(fontsize=8, ncols=2, loc="upper left")
    ax.grid(axis="y", alpha=0.3)
    plt.tight_layout(); fig.savefig(results_dir / "fig3b_arrival_gap_percentiles.png", dpi=160); plt.close(fig)
    written.append("fig3b_arrival_gap_percentiles.png")

    # Figure 4: perceived delay vs added latency.
    fig, ax = plt.subplots(figsize=(8.5, 5))
    latency_conditions = [("A_clean", 0), ("B_lat100", 100), ("C_lat200", 200)]
    for mode, marker in [("baseline", "o"), ("optimized", "s")]:
        xs, ys, es = [], [], []
        for cond, lat in latency_conditions:
            row = df[(df.condition == cond) & (df["mode"] == mode)]
            if row.empty:
                continue
            xs.append(lat)
            ys.append(float(row["mean_perceived_ms"].iloc[0]))
            es.append(float(row["perceived_ci95_ms"].iloc[0]))
        ax.errorbar(xs, ys, yerr=es, marker=marker, capsize=3, label=mode, linewidth=2)
    ax.set_xlabel("Simulated added round-trip latency (ms)")
    ax.set_ylabel("Perceived input-to-visible delay (ms)")
    ax.set_title("Perceived responsiveness vs network latency")
    ax.legend()
    ax.grid(alpha=0.3)
    plt.tight_layout(); fig.savefig(results_dir / "fig4_perceived_vs_latency.png", dpi=160); plt.close(fig)
    written.append("fig4_perceived_vs_latency.png")

    # Figure 6 (appendix): empirical CDF of ack latency across the three
    # latency conditions (A_clean / B_lat100 / C_lat200). Under the default
    # main matrix (zero loss, zero jitter) both modes produce near step
    # functions that sit almost on top of each other - that is the correct
    # result but it is visually uninteresting and should be treated as an
    # appendix chart. The CDF concept earns its keep in the loss-sweep
    # figure (fig9), where tail divergence between the modes is visible.
    fig, ax = plt.subplots(figsize=(8.5, 5))
    any_plotted = False
    for cond in ["A_clean", "B_lat100", "C_lat200"]:
        for mode, ls in [("baseline", "--"), ("optimized", "-")]:
            samples = []
            for run_dir in exp_dir.iterdir():
                if not run_dir.is_dir():
                    continue
                m = RUN_RE.match(run_dir.name)
                if not m or m["cond"] != cond or m["mode"] != mode:
                    continue
                df_in = load_inputs(run_dir)
                if df_in.empty or "rtt_ms" not in df_in.columns:
                    continue
                start_t = df_in["wall_time_ms"].min()
                body = df_in[df_in["wall_time_ms"] >= start_t + 500]
                samples.extend(body["rtt_ms"].tolist())
            if not samples:
                continue
            arr = np.sort(np.array(samples, dtype=float))
            ys = np.arange(1, len(arr) + 1) / len(arr)
            ax.plot(arr, ys, linestyle=ls, linewidth=1.7,
                    label=f"{cond} / {mode}")
            any_plotted = True
    if any_plotted:
        ax.set_xlabel("Authoritative ack latency (ms)")
        ax.set_ylabel("Empirical CDF")
        ax.set_title("Ack latency distribution across latency conditions")
        ax.grid(alpha=0.3)
        ax.legend(fontsize=8, loc="lower right")
        plt.tight_layout(); fig.savefig(results_dir / "fig6_ack_latency_cdf.png", dpi=160)
        written.append("fig6_ack_latency_cdf.png")
    plt.close(fig)

    return written


# ---- isolation mode ---------------------------------------------------------

def analyze_isolation(exp_dir: Path, results_dir: Path) -> List[str]:
    results_dir.mkdir(parents=True, exist_ok=True)
    runs = discover(exp_dir, ISOLATION_RE)
    if not runs:
        print(f"no isolation runs found under {exp_dir}", file=sys.stderr)
        return []

    aggs: List[RunAggregates] = []
    for d in runs:
        m = ISOLATION_RE.match(d.name)
        assert m is not None
        aggs.append(aggregate_generic(d, condition="B_lat100",
                                      mode=m["variant"], rep=int(m["rep"])))

    pd.DataFrame([a.__dict__ for a in aggs]).to_csv(results_dir / "per_run.csv", index=False)

    # Aggregate by variant.
    grouped: Dict[str, List[RunAggregates]] = defaultdict(list)
    for r in aggs:
        grouped[r.mode].append(r)

    rows = []
    for variant in ISOLATION_ORDER:
        reps = grouped.get(variant, [])
        if not reps:
            continue
        perceived_mean, perceived_ci = _agg_stat([r.mean_perceived_ms for r in reps])
        delay_mean, delay_ci = _agg_stat([r.mean_delay_ms for r in reps])
        bw_mean, bw_ci = _agg_stat([r.bandwidth_kbps_mean for r in reps])
        cv_mean, cv_ci = _agg_stat([r.snapshot_arrival_cv for r in reps])
        rows.append({
            "variant": variant, "reps": len(reps),
            "mean_perceived_ms": perceived_mean, "perceived_ci95_ms": perceived_ci,
            "mean_delay_ms": delay_mean, "delay_ci95_ms": delay_ci,
            "bandwidth_kbps": bw_mean, "bandwidth_ci95": bw_ci,
            "snapshot_arrival_cv": cv_mean, "snapshot_arrival_cv_ci95": cv_ci,
        })
    summary = pd.DataFrame(rows)
    summary.to_csv(results_dir / "summary.csv", index=False)

    # Figure 5: isolation decomposition (bar chart of perceived delay + ack).
    variants_present = [v for v in ISOLATION_ORDER if v in summary["variant"].values]
    if not variants_present:
        return []

    fig, ax = plt.subplots(figsize=(9.5, 5.5))
    x = np.arange(len(variants_present))
    width = 0.38
    perceived = [float(summary[summary.variant == v]["mean_perceived_ms"].iloc[0]) for v in variants_present]
    perceived_ci = [float(summary[summary.variant == v]["perceived_ci95_ms"].iloc[0]) for v in variants_present]
    ack = [float(summary[summary.variant == v]["mean_delay_ms"].iloc[0]) for v in variants_present]
    ack_ci = [float(summary[summary.variant == v]["delay_ci95_ms"].iloc[0]) for v in variants_present]

    ax.bar(x - width/2, ack, width, yerr=ack_ci, capsize=3,
           label="Authoritative ack latency (ms)")
    ax.bar(x + width/2, perceived, width, yerr=perceived_ci, capsize=3,
           label="Perceived input-to-visible delay (ms)")
    ax.set_xticks(x)
    ax.set_xticklabels([ISOLATION_LABELS.get(v, v) for v in variants_present], fontsize=9)
    ax.set_ylabel("Milliseconds (lower is better)")
    ax.set_title("Effect of each optimization at 100 ms added RTT")
    ax.legend()
    ax.grid(axis="y", alpha=0.3)
    plt.tight_layout(); fig.savefig(results_dir / "fig5_isolation_decomp.png", dpi=160); plt.close(fig)

    return ["fig5_isolation_decomp.png"]


# ---- loss sweep mode --------------------------------------------------------

def analyze_loss(exp_dir: Path, results_dir: Path) -> List[str]:
    results_dir.mkdir(parents=True, exist_ok=True)
    runs = discover(exp_dir, LOSS_RE)
    if not runs:
        print(f"no loss-sweep runs found under {exp_dir}", file=sys.stderr)
        return []

    # Per-run aggregates, plus direct collection of per-input rtt_ms samples
    # for the p50/p95/p99 lines.
    aggs: List[RunAggregates] = []
    loss_points: Dict[Tuple[int, str], Dict[str, list]] = defaultdict(lambda: {"rtt": [], "cv": []})
    for d in runs:
        m = LOSS_RE.match(d.name)
        assert m is not None
        loss = int(m["loss"]); mode = m["mode"]; rep = int(m["rep"])
        agg = aggregate_generic(d, condition=f"loss{loss}", mode=mode, rep=rep)
        aggs.append(agg)

        df_in = load_inputs(d)
        if not df_in.empty and "rtt_ms" in df_in.columns:
            start_t = df_in["wall_time_ms"].min()
            body = df_in[df_in["wall_time_ms"] >= start_t + 500]
            loss_points[(loss, mode)]["rtt"].extend(body["rtt_ms"].tolist())
        if not math.isnan(agg.snapshot_arrival_cv):
            loss_points[(loss, mode)]["cv"].append(agg.snapshot_arrival_cv)

    pd.DataFrame([a.__dict__ for a in aggs]).to_csv(results_dir / "per_run.csv", index=False)

    # Summary rows with percentiles.
    rows = []
    losses = sorted({k[0] for k in loss_points.keys()})
    for loss in losses:
        for mode in MODES:
            bucket = loss_points.get((loss, mode))
            if not bucket or not bucket["rtt"]:
                continue
            arr = np.array(bucket["rtt"], dtype=float)
            rows.append({
                "loss_pct": loss, "mode": mode, "samples": int(len(arr)),
                "p50_ms": float(np.quantile(arr, 0.50)),
                "p95_ms": float(np.quantile(arr, 0.95)),
                "p99_ms": float(np.quantile(arr, 0.99)),
                "mean_ms": float(arr.mean()),
                "snapshot_arrival_cv": float(np.mean(bucket["cv"])) if bucket["cv"] else math.nan,
            })
    summary = pd.DataFrame(rows)
    summary.to_csv(results_dir / "summary.csv", index=False)

    written: List[str] = []

    # Figure 9: p50/p95/p99 vs loss percentage for both modes.
    fig, ax = plt.subplots(figsize=(9, 5.5))
    for mode, marker in [("baseline", "o"), ("optimized", "s")]:
        sub = summary[summary["mode"] == mode].sort_values("loss_pct")
        if sub.empty:
            continue
        ax.plot(sub["loss_pct"], sub["p50_ms"], marker=marker, linewidth=1.6,
                linestyle="-", label=f"{mode} p50")
        ax.plot(sub["loss_pct"], sub["p95_ms"], marker=marker, linewidth=1.6,
                linestyle="--", label=f"{mode} p95")
        ax.plot(sub["loss_pct"], sub["p99_ms"], marker=marker, linewidth=1.6,
                linestyle=":", label=f"{mode} p99")
    ax.set_xlabel("Simulated packet loss (%)")
    ax.set_ylabel("Ack latency (ms)")
    ax.set_title("Ack latency tails vs packet loss (100 ms base RTT)")
    ax.grid(alpha=0.3)
    ax.legend(fontsize=8, ncols=2)
    plt.tight_layout(); fig.savefig(results_dir / "fig9_loss_delay_tail.png", dpi=160); plt.close(fig)
    written.append("fig9_loss_delay_tail.png")

    # Figure 10: snapshot arrival CV vs loss percentage.
    fig, ax = plt.subplots(figsize=(9, 5.5))
    for mode, marker in [("baseline", "o"), ("optimized", "s")]:
        sub = summary[summary["mode"] == mode].sort_values("loss_pct")
        if sub.empty or sub["snapshot_arrival_cv"].isna().all():
            continue
        ax.plot(sub["loss_pct"], sub["snapshot_arrival_cv"],
                marker=marker, linewidth=1.8, label=mode)
    ax.set_xlabel("Simulated packet loss (%)")
    ax.set_ylabel("Snapshot arrival CV (lower = smoother)")
    ax.set_title("Delivery jitter vs packet loss (100 ms base RTT)")
    ax.grid(alpha=0.3)
    ax.legend()
    plt.tight_layout(); fig.savefig(results_dir / "fig10_snapshot_jitter_cv.png", dpi=160); plt.close(fig)
    written.append("fig10_snapshot_jitter_cv.png")

    return written


# ---- scale sweep mode -------------------------------------------------------

def analyze_scale(exp_dir: Path, results_dir: Path) -> List[str]:
    results_dir.mkdir(parents=True, exist_ok=True)
    runs = discover(exp_dir, SCALE_RE)
    if not runs:
        print(f"no scale-sweep runs found under {exp_dir}", file=sys.stderr)
        return []

    aggs: List[RunAggregates] = []
    for d in runs:
        m = SCALE_RE.match(d.name)
        assert m is not None
        n = int(m["n"]); mode = m["mode"]; rep = int(m["rep"])
        aggs.append(aggregate_generic(d, condition=f"N{n}", mode=mode, rep=rep))

    pd.DataFrame([a.__dict__ for a in aggs]).to_csv(results_dir / "per_run.csv", index=False)

    # Aggregate by (N, mode).
    grouped: Dict[Tuple[int, str], List[RunAggregates]] = defaultdict(list)
    for r in aggs:
        n = int(r.condition[1:])
        grouped[(n, r.mode)].append(r)

    rows = []
    ns = sorted({k[0] for k in grouped.keys()})
    for n in ns:
        for mode in MODES:
            reps = grouped.get((n, mode), [])
            if not reps:
                continue
            bw_mean, bw_ci = _agg_stat([r.bandwidth_kbps_mean for r in reps])
            bps_mean, bps_ci = _agg_stat([r.bytes_per_snapshot for r in reps])
            sr_mean, sr_ci = _agg_stat([r.snapshot_rate_hz for r in reps])
            rows.append({
                "players": n, "mode": mode, "reps": len(reps),
                "bandwidth_kbps": bw_mean, "bandwidth_ci95": bw_ci,
                "bytes_per_snapshot": bps_mean, "bytes_per_snapshot_ci95": bps_ci,
                "snapshot_rate_hz": sr_mean, "snapshot_rate_ci95": sr_ci,
            })
    summary = pd.DataFrame(rows)
    summary.to_csv(results_dir / "summary.csv", index=False)

    written: List[str] = []

    # Figure 7: bytes per wire packet vs N. The server log counts bytes across all
    # recipients (one broadcast = N sends); dividing by N recovers per-packet size.
    # Analytical overlay for optimized mode: 7-byte header (u8 type + u32 tick +
    # u16 count) + 22-byte per-player record (u16 id + 4xf32 state + u32 seq).
    fig, ax = plt.subplots(figsize=(9, 5.5))
    for mode, marker in [("baseline", "o"), ("optimized", "s")]:
        sub = summary[summary["mode"] == mode].sort_values("players")
        if sub.empty:
            continue
        bytes_per_packet = sub["bytes_per_snapshot"] / sub["players"]
        yerr = sub["bytes_per_snapshot_ci95"] / sub["players"]
        ax.errorbar(sub["players"], bytes_per_packet, yerr=yerr,
                    marker=marker, capsize=3, linewidth=1.6,
                    label=f"{mode} measured")
    xs = np.array(sorted({k[0] for k in grouped.keys()}))
    if len(xs):
        ax.plot(xs, 7 + 22 * xs, linestyle="--",
                label="optimized analytical (7 + 22*N)")
    ax.set_xlabel("Players per session (N)")
    ax.set_ylabel("Bytes per snapshot packet on the wire")
    ax.set_title("Snapshot payload size vs player count")
    ax.grid(alpha=0.3)
    ax.legend()
    plt.tight_layout(); fig.savefig(results_dir / "fig7_bytes_per_snapshot.png", dpi=160); plt.close(fig)
    written.append("fig7_bytes_per_snapshot.png")

    # Figure 8: bandwidth per client vs N.
    fig, ax = plt.subplots(figsize=(9, 5.5))
    for mode, marker in [("baseline", "o"), ("optimized", "s")]:
        sub = summary[summary["mode"] == mode].sort_values("players")
        if sub.empty:
            continue
        ax.errorbar(sub["players"], sub["bandwidth_kbps"],
                    yerr=sub["bandwidth_ci95"],
                    marker=marker, capsize=3, linewidth=1.8, label=mode)
    ax.set_xlabel("Players per session (N)")
    ax.set_ylabel("Bandwidth per client (kbps)")
    ax.set_title("Downlink bandwidth vs player count (clean network)")
    ax.grid(alpha=0.3)
    ax.legend()
    plt.tight_layout(); fig.savefig(results_dir / "fig8_bandwidth_scaling.png", dpi=160); plt.close(fig)
    written.append("fig8_bandwidth_scaling.png")

    return written


# ---- entrypoint -------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", default="main",
                    choices=["main", "isolation", "loss", "scale"])
    ap.add_argument("--experiments-dir", default=None,
                    help="override input directory (defaults depend on --mode)")
    ap.add_argument("--results-dir", default=None,
                    help="override output directory (defaults depend on --mode)")
    args = ap.parse_args()

    default_exp = {
        "main":      "experiments",
        "isolation": "experiments_isolation",
        "loss":      "experiments_loss",
        "scale":     "experiments_scale",
    }[args.mode]
    default_results = {
        "main":      "results",
        "isolation": "results_isolation",
        "loss":      "results_loss",
        "scale":     "results_scale",
    }[args.mode]

    exp_dir = Path(args.experiments_dir or default_exp)
    results_dir = Path(args.results_dir or default_results)
    if not exp_dir.exists():
        print(f"no experiments directory at {exp_dir}", file=sys.stderr)
        return 2

    if args.mode == "main":
        runs = discover(exp_dir, RUN_RE)
        if not runs:
            print(f"no runs found under {exp_dir}", file=sys.stderr)
            return 3
        aggs = [aggregate_run(r) for r in runs]
        results_dir.mkdir(parents=True, exist_ok=True)
        pd.DataFrame([a.__dict__ for a in aggs]).to_csv(results_dir / "per_run.csv", index=False)
        summary = summarize_main(aggs)
        summary.to_csv(results_dir / "summary.csv", index=False)
        written = plot_main(summary, aggs, exp_dir, results_dir)
    elif args.mode == "isolation":
        written = analyze_isolation(exp_dir, results_dir)
    elif args.mode == "loss":
        written = analyze_loss(exp_dir, results_dir)
    elif args.mode == "scale":
        written = analyze_scale(exp_dir, results_dir)
    else:
        return 2

    print("wrote:")
    print(f"  {results_dir/'summary.csv'}")
    print(f"  {results_dir/'per_run.csv'}")
    for name in written:
        print(f"  {results_dir/name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
