#!/usr/bin/env python3
"""Plot latency reports produced by order_book_latency.exe.

Dependency: matplotlib (and its NumPy dependency).
Run from the repository root:
    py -m pip install matplotlib
    py tools/plot_latency.py
or pass a specific benchmark_results/latency_... directory.
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path
from collections import defaultdict


def latest_report(root: Path) -> Path:
    candidates = [
        path for path in root.glob("latency_*")
        if path.is_dir()
        and (path / "summary.csv").is_file()
        and (path / "samples.csv").is_file()
    ]
    if not candidates:
        raise FileNotFoundError(
            f"No latency reports found under {root}. Run order_book_latency.exe first."
        )
    return max(candidates, key=lambda path: path.name)


def load_report(run_dir: Path):
    with (run_dir / "summary.csv").open(newline="", encoding="utf-8") as handle:
        summary = list(csv.DictReader(handle))

    samples_by_workflow: dict[str, list[int]] = defaultdict(list)
    with (run_dir / "samples.csv").open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            samples_by_workflow[row["workflow"]].append(int(row["latency_ns"]))

    if not summary or not samples_by_workflow:
        raise ValueError(f"Report is empty or incomplete: {run_dir}")
    return summary, samples_by_workflow


def safe_filename(value: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9_-]+", "_", value).strip("_").lower()
    return slug or "workflow"


def plot_percentiles(run_dir: Path, rows: list[dict[str, str]]) -> Path:
    import matplotlib.pyplot as plt

    labels = [row["workflow"] for row in rows]
    metrics = [
        ("median_ns", "p50 / median"),
        ("p95_ns", "p95"),
        ("p99_ns", "p99"),
        ("p99_9_ns", "p99.9"),
    ]

    x = list(range(len(rows)))
    width = 0.19
    fig, ax = plt.subplots(figsize=(max(11, len(rows) * 1.45), 6.5))

    for j, (field, label) in enumerate(metrics):
        offsets = [value + (j - 1.5) * width for value in x]
        values = [max(1.0, float(row[field])) for row in rows]
        ax.bar(offsets, values, width=width, label=label)

    ax.set_yscale("log")
    ax.set_ylabel("Latency (ns, logarithmic scale)")
    ax.set_title("Order-book latency percentiles")
    ax.set_xticks(x, labels, rotation=28, ha="right")
    ax.grid(axis="y", which="both", alpha=0.25)
    ax.legend()
    fig.tight_layout()

    output = run_dir / "latency_percentiles.png"
    fig.savefig(output, dpi=160)
    plt.close(fig)
    return output


def plot_ecdf(
    run_dir: Path,
    samples_by_workflow: dict[str, list[int]],
) -> Path:
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(10.5, 6.5))

    for workflow, raw_values in samples_by_workflow.items():
        values = sorted(raw_values)
        # Bound rendering time while retaining the curve's endpoints and tail.
        stride = max(1, len(values) // 5000)
        indexes = list(range(0, len(values), stride))
        if indexes[-1] != len(values) - 1:
            indexes.append(len(values) - 1)

        x_values = [max(1, values[index]) for index in indexes]
        y_values = [(index + 1) * 100.0 / len(values) for index in indexes]
        ax.plot(x_values, y_values, label=workflow, linewidth=1.5)

    ax.set_xscale("log")
    ax.set_xlim(left=1)
    ax.set_ylim(0, 100)
    ax.set_xlabel("Latency (ns, logarithmic scale; zero values floored to 1 for display)")
    ax.set_ylabel("Empirical cumulative percentage")
    ax.set_title("Empirical latency distributions")
    ax.grid(which="both", alpha=0.25)
    ax.legend(fontsize="small")
    fig.tight_layout()

    output = run_dir / "latency_ecdf.png"
    fig.savefig(output, dpi=160)
    plt.close(fig)
    return output


def plot_traces(
    run_dir: Path,
    samples_by_workflow: dict[str, list[int]],
) -> list[Path]:
    import matplotlib.pyplot as plt

    outputs: list[Path] = []
    for workflow, values in samples_by_workflow.items():
        fig, ax = plt.subplots(figsize=(10.5, 4.8))
        # Clamp zero-nanosecond rounded samples to 1 only for log display.
        display_values = [max(1, value) for value in values]
        ax.plot(range(len(values)), display_values, linewidth=0.7)
        ax.set_yscale("log")
        ax.set_xlabel("Sample index (measurement order)")
        ax.set_ylabel("Latency (ns, logarithmic scale)")
        ax.set_title(f"Latency trace: {workflow}")
        ax.grid(which="both", alpha=0.25)
        fig.tight_layout()

        output = run_dir / f"trace_{safe_filename(workflow)}.png"
        fig.savefig(output, dpi=150)
        plt.close(fig)
        outputs.append(output)

    return outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "report_dir",
        nargs="?",
        type=Path,
        help="Specific latency report directory (defaults to latest report)",
    )
    args = parser.parse_args()

    root = Path("benchmark_results")
    run_dir = args.report_dir if args.report_dir else latest_report(root)
    run_dir = run_dir.resolve()

    if not (run_dir / "summary.csv").is_file():
        raise FileNotFoundError(f"Missing summary.csv in {run_dir}")
    if not (run_dir / "samples.csv").is_file():
        raise FileNotFoundError(f"Missing samples.csv in {run_dir}")

    summary, samples = load_report(run_dir)
    generated = [
        plot_percentiles(run_dir, summary),
        plot_ecdf(run_dir, samples),
        *plot_traces(run_dir, samples),
    ]

    print(f"Loaded {len(summary)} workflows from: {run_dir}")
    print("Generated plots:")
    for path in generated:
        print(f"  {path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        raise SystemExit(f"Error: {error}")
