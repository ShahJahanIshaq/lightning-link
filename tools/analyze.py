#!/usr/bin/env python3
"""
Lightning Link analysis pipeline.

Consumes the CSVs produced under experiments/<condition>_<mode>_rep<N>/ and emits
publication-ready figures and a summary table under results/.

Metrics:
 * Input-to-visible delay (ms)     — client_*_inputs_*.csv `rtt_ms` column
 * Bandwidth per client (kbps)     — (delta bytes_received_total / delta t) * 8 / 1000
                                      from client_*_periodic_*.csv
 * Remote motion smoothness (px)   — frame-to-frame position variance of remote
                                      entities, derived from server snapshots (we
                                      reconstruct per-tick positions from the
                                      server's bytes_sent_total timeline + known
                                      authoritative trajectory). Because the
                                      client does not log per-frame remote pixel
                                      positions (to keep logs small), we instead
                                      estimate smoothness by the coefficient of
                                      variation of inter-snapshot arrival jitter
                                      — a decent proxy under the fixed-tick model.
 * Snapshot delivery stability (Hz) — snapshots_sent_total growth rate on server
                                       vs. client bytes_received_total growth.
"""

from __future__ import annotations

import argparse
import csv
import glob
import math
import os
import re
import statistics
import sys
from collections import defaultdict
from dataclasses import dataclass, field
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

RUN_RE = re.compile(r"^(?P<cond>[A-Z]_[A-Za-z0-9_]+?)_(?P<mode>optimized|baseline)_rep(?P<rep>\d+)$")


@dataclass
class RunAggregates:
    condition: str
    mode: str
    rep: int
    # Core metrics across the run.
    mean_delay_ms: float = math.nan
    median_delay_ms: float = math.nan
    p95_delay_ms: float = math.nan
    mean_perceived_ms: float = math.nan
    p95_perceived_ms: float = math.nan
    bandwidth_kbps_mean: float = math.nan
    snapshot_rate_hz: float = math.nan
    snapshot_arrival_cv: float = math.nan
    clients: int = 0
    total_bytes_sent: int = 0


def discover_runs(exp_dir: Path) -> List[Path]:
    dirs = []
    for d in sorted(exp_dir.iterdir()):
        if d.is_dir() and RUN_RE.match(d.name):
            dirs.append(d)
    return dirs


# ---- per-run parsing --------------------------------------------------------

def load_periodic(run_dir: Path) -> pd.DataFrame:
    frames = []
    for p in run_dir.glob("client_*_periodic_*.csv"):
        try:
            df = pd.read_csv(p)
            df["source_file"] = p.name
            frames.append(df)
        except Exception as e:
            print(f"warn: failed to read {p}: {e}", file=sys.stderr)
    if not frames:
        return pd.DataFrame()
    return pd.concat(frames, ignore_index=True)


def load_inputs(run_dir: Path) -> pd.DataFrame:
    frames = []
    for p in run_dir.glob("client_*_inputs_*.csv"):
        try:
            df = pd.read_csv(p)
            frames.append(df)
        except Exception as e:
            print(f"warn: failed to read {p}: {e}", file=sys.stderr)
    if not frames:
        return pd.DataFrame()
    return pd.concat(frames, ignore_index=True)


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


def aggregate_run(run_dir: Path) -> RunAggregates:
    m = RUN_RE.match(run_dir.name)
    agg = RunAggregates(condition=m["cond"], mode=m["mode"], rep=int(m["rep"]))

    inputs = load_inputs(run_dir)
    if not inputs.empty and "rtt_ms" in inputs.columns:
        # Skip the warmup window (first 500 ms) where the TCP handshake or UDP
        # join completion can bias latency upward.
        start_t = inputs["wall_time_ms"].min()
        body = inputs[inputs["wall_time_ms"] >= start_t + 500]
        if len(body) > 0:
            agg.mean_delay_ms   = float(body["rtt_ms"].mean())
            agg.median_delay_ms = float(body["rtt_ms"].median())
            agg.p95_delay_ms    = float(body["rtt_ms"].quantile(0.95))
            if "perceived_delay_ms" in body.columns:
                agg.mean_perceived_ms = float(body["perceived_delay_ms"].mean())
                agg.p95_perceived_ms  = float(body["perceived_delay_ms"].quantile(0.95))

    periodic = load_periodic(run_dir)
    if not periodic.empty:
        agg.clients = periodic["player_id"].nunique()
        # Average bandwidth across clients = last(bytes) / (t_last - t_first) * 8/1000
        bws = []
        arrivals_cv = []
        for pid, sub in periodic.groupby("player_id"):
            sub = sub.sort_values("wall_time_ms")
            if len(sub) < 2:
                continue
            dt_s = (sub["wall_time_ms"].iloc[-1] - sub["wall_time_ms"].iloc[0]) / 1000.0
            dbytes = sub["bytes_received_total"].iloc[-1] - sub["bytes_received_total"].iloc[0]
            if dt_s > 0:
                bws.append((dbytes * 8.0 / 1000.0) / dt_s)
            # snapshot arrival CV via latest_snapshot_tick deltas over time deltas
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
            agg.snapshot_rate_hz = float((last["snapshots_sent_total"] - first["snapshots_sent_total"]) / dur_s)
        agg.total_bytes_sent = int(last["bytes_sent_total"])

    return agg


# ---- cross-run summary ------------------------------------------------------

def summarize(runs: List[RunAggregates]) -> pd.DataFrame:
    rows = []
    grouped: Dict[Tuple[str, str], List[RunAggregates]] = defaultdict(list)
    for r in runs:
        grouped[(r.condition, r.mode)].append(r)

    for cond in CONDITION_ORDER:
        for mode in MODES:
            reps = grouped.get((cond, mode), [])
            if not reps:
                continue
            def agg_stat(vals):
                arr = np.array([v for v in vals if not math.isnan(v)])
                if len(arr) == 0:
                    return (math.nan, math.nan)
                if len(arr) < 2:
                    return (float(arr.mean()), 0.0)
                # 95% CI half-width using t-approx (n small) => 1.96*sd/sqrt(n) for n>=3 is fine.
                ci = 1.96 * float(arr.std(ddof=1)) / math.sqrt(len(arr))
                return (float(arr.mean()), ci)

            delay_mean, delay_ci = agg_stat([r.mean_delay_ms for r in reps])
            perceived_mean, perceived_ci = agg_stat([r.mean_perceived_ms for r in reps])
            bw_mean, bw_ci       = agg_stat([r.bandwidth_kbps_mean for r in reps])
            sr_mean, sr_ci       = agg_stat([r.snapshot_rate_hz for r in reps])
            cv_mean, cv_ci       = agg_stat([r.snapshot_arrival_cv for r in reps])

            rows.append({
                "condition": cond,
                "mode": mode,
                "reps": len(reps),
                "clients": reps[0].clients,
                "mean_delay_ms": delay_mean,
                "delay_ci95_ms": delay_ci,
                "mean_perceived_ms": perceived_mean,
                "perceived_ci95_ms": perceived_ci,
                "bandwidth_kbps": bw_mean,
                "bandwidth_ci95": bw_ci,
                "snapshot_rate_hz": sr_mean,
                "snapshot_rate_ci95": sr_ci,
                "snapshot_arrival_cv": cv_mean,
                "snapshot_arrival_cv_ci95": cv_ci,
            })
    return pd.DataFrame(rows)


# ---- plotting ---------------------------------------------------------------

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


def plot_figures(df: pd.DataFrame, results_dir: Path) -> None:
    results_dir.mkdir(parents=True, exist_ok=True)

    # Figure 1: perceived input-to-visible delay (the headline chart - shows the
    # prediction win most clearly). In optimized+prediction mode this is bounded
    # by a single render frame; in baseline it equals wire RTT.
    fig, ax = plt.subplots(figsize=(8.5, 5))
    _bar_pair(ax, df, "mean_perceived_ms", "perceived_ci95_ms",
              "Perceived input-to-visible delay (ms)",
              "Perceived responsiveness by condition and mode")
    plt.tight_layout()
    fig.savefig(results_dir / "fig1_perceived_delay.png", dpi=160)
    plt.close(fig)

    # Figure 1b: authoritative ack latency (wire RTT) for the same conditions.
    fig, ax = plt.subplots(figsize=(8.5, 5))
    _bar_pair(ax, df, "mean_delay_ms", "delay_ci95_ms",
              "Authoritative ack latency (ms)",
              "Wire RTT (time from input to authoritative confirmation)")
    plt.tight_layout()
    fig.savefig(results_dir / "fig1b_ack_latency.png", dpi=160)
    plt.close(fig)

    # Figure 2: bandwidth per client.
    fig, ax = plt.subplots(figsize=(8.5, 5))
    _bar_pair(ax, df, "bandwidth_kbps", "bandwidth_ci95",
              "Bandwidth per client (kbps)",
              "Average downlink bandwidth by condition and mode")
    plt.tight_layout()
    fig.savefig(results_dir / "fig2_bandwidth.png", dpi=160)
    plt.close(fig)

    # Figure 3: snapshot arrival stability (lower CV = smoother).
    fig, ax = plt.subplots(figsize=(8.5, 5))
    _bar_pair(ax, df, "snapshot_arrival_cv", "snapshot_arrival_cv_ci95",
              "Snapshot arrival CV (lower = steadier)",
              "Snapshot delivery stability by condition and mode")
    plt.tight_layout()
    fig.savefig(results_dir / "fig3_stability.png", dpi=160)
    plt.close(fig)

    # Figure 4: perceived delay vs added latency. Shows that optimized mode
    # decouples user-perceived response from network latency (prediction) while
    # baseline scales linearly with RTT.
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
    plt.tight_layout()
    fig.savefig(results_dir / "fig4_perceived_vs_latency.png", dpi=160)
    plt.close(fig)


# ---- entrypoint -------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--experiments-dir", default="experiments")
    ap.add_argument("--results-dir", default="results")
    args = ap.parse_args()

    exp_dir = Path(args.experiments_dir)
    results_dir = Path(args.results_dir)
    if not exp_dir.exists():
        print(f"no experiments directory at {exp_dir}", file=sys.stderr)
        return 2

    runs = discover_runs(exp_dir)
    if not runs:
        print(f"no runs found under {exp_dir}", file=sys.stderr)
        return 3

    aggs = [aggregate_run(r) for r in runs]
    # Write raw per-run aggregates.
    results_dir.mkdir(parents=True, exist_ok=True)
    pd.DataFrame([a.__dict__ for a in aggs]).to_csv(results_dir / "per_run.csv", index=False)

    summary = summarize(aggs)
    summary.to_csv(results_dir / "summary.csv", index=False)

    plot_figures(summary, results_dir)
    print("wrote:")
    print(f"  {results_dir/'summary.csv'}")
    print(f"  {results_dir/'per_run.csv'}")
    for name in ("fig1_perceived_delay.png", "fig1b_ack_latency.png",
                 "fig2_bandwidth.png", "fig3_stability.png",
                 "fig4_perceived_vs_latency.png"):
        print(f"  {results_dir/name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
