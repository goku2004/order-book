#!/usr/bin/env python3
"""Plot Google Benchmark CSV output from the order-book throughput benchmark.

The Google Benchmark CSV contains a short environment preamble before its header.
This reader finds the header automatically and plots only mean aggregate rows.

Usage from repository root:
    py -m pip install matplotlib
    py tools/plot_throughput.py throughput.csv
"""

from __future__ import annotations

import argparse
import csv
import math
import re
from datetime import datetime
from pathlib import Path
from collections import defaultdict


def read_google_benchmark_csv(csv_path: Path) -> list[dict[str, object]]:
    lines = csv_path.read_text(encoding="utf-8-sig").splitlines()
    header_index = next(
        (
            index for index, line in enumerate(lines)
            if line.startswith("name,iterations,real_time,cpu_time")
        ),
        None,
    )
    if header_index is None:
        raise ValueError(
            f"Could not locate a Google Benchmark CSV header in {csv_path}"
        )

    parsed: list[dict[str, object]] = []
    reader = csv.DictReader(lines[header_index:])
    for row in reader:
        name = row.get("name", "")
        if not name.endswith("_mean"):
            continue

        raw_rate = row.get("items_per_second", "")
        if not raw_rate:
            continue
        try:
            rate = float(raw_rate)
        except ValueError:
            continue
        if not math.isfinite(rate) or rate <= 0:
            continue

        mean_name = name[:-len("_mean")]
        parsed.append({
            "name": mean_name,
            "items_per_second": rate,
            "real_time": float(row["real_time"]),
            "cpu_time": float(row["cpu_time"]),
            "time_unit": row.get("time_unit", ""),
        })

    if not parsed:
        raise ValueError("No valid mean rows with positive items_per_second found")
    return parsed


def make_output_dir(csv_path: Path) -> Path:
    root = Path("benchmark_results")
    root.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output = root / f"throughput_analysis_{csv_path.stem}_{stamp}"
    suffix = 1
    while output.exists():
        output = root / f"throughput_analysis_{csv_path.stem}_{stamp}_{suffix}"
        suffix += 1
    output.mkdir(parents=True)
    return output


def plot_overview(rows: list[dict[str, object]], output_dir: Path) -> Path:
    import matplotlib.pyplot as plt

    ordered = sorted(rows, key=lambda row: float(row["items_per_second"]))
    labels = [
        str(row["name"]).replace("BM_", "").replace("_", " ")
        for row in ordered
    ]
    values = [float(row["items_per_second"]) for row in ordered]

    fig, ax = plt.subplots(figsize=(12, max(7, len(rows) * 0.34)))
    ax.barh(range(len(ordered)), values)
    ax.set_yticks(range(len(ordered)), labels)
    ax.set_xscale("log")
    ax.set_xlabel("Google Benchmark items processed per second (log scale)")
    ax.set_title("Order-book throughput by workload")
    ax.grid(axis="x", which="both", alpha=0.25)
    fig.tight_layout()

    path = output_dir / "throughput_overview.png"
    fig.savefig(path, dpi=160)
    plt.close(fig)
    return path


def plot_batch_scaling(
    rows: list[dict[str, object]],
    output_dir: Path,
) -> Path | None:
    import matplotlib.pyplot as plt

    # These one-argument cases use the argument as an operations-per-batch size.
    eligible_prefixes = (
        "BM_SingleLevel_",
        "BM_NonCrossing",
        "BM_Partial",
        "BM_MixedWorkload",
    )
    series: dict[str, list[tuple[int, float]]] = defaultdict(list)

    for row in rows:
        name = str(row["name"])
        match = re.fullmatch(r"([^/]+)/(\d+)", name)
        if not match:
            continue
        family, batch = match.group(1), int(match.group(2))
        if not family.startswith(eligible_prefixes):
            continue
        series[family.removeprefix("BM_")].append(
            (batch, float(row["items_per_second"]))
        )

    series = {
        family: sorted(points)
        for family, points in series.items()
        if len(points) >= 2
    }
    if not series:
        return None

    fig, ax = plt.subplots(figsize=(10.5, 6.5))
    for family, points in sorted(series.items()):
        ax.plot(
            [point[0] for point in points],
            [point[1] for point in points],
            marker="o",
            linewidth=1.6,
            label=family.replace("_", " "),
        )

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Benchmark argument (log scale)")
    ax.set_ylabel("Items processed per second (log scale)")
    ax.set_title("Throughput scaling with batch size")
    ax.grid(which="both", alpha=0.25)
    ax.legend(fontsize="small")
    fig.tight_layout()

    path = output_dir / "throughput_scaling.png"
    fig.savefig(path, dpi=160)
    plt.close(fig)
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "csv_file",
        nargs="?",
        type=Path,
        default=Path("throughput.csv"),
        help="Google Benchmark CSV path (default: throughput.csv)",
    )
    args = parser.parse_args()
    csv_path = args.csv_file.resolve()
    if not csv_path.is_file():
        raise FileNotFoundError(f"CSV file not found: {csv_path}")

    rows = read_google_benchmark_csv(csv_path)
    output_dir = make_output_dir(csv_path)
    overview = plot_overview(rows, output_dir)
    scaling = plot_batch_scaling(rows, output_dir)

    with (output_dir / "summary.md").open("w", encoding="utf-8") as handle:
        handle.write("# Throughput benchmark report\n\n")
        handle.write(f"Input CSV: {csv_path}\n\n")
        handle.write(
            "This report includes only mean aggregates with positive, finite "
            "items_per_second values. The overview compares different workload "
            "types; it should not be interpreted as a fair ranking when workloads "
            "have different amounts of work per operation.\n\n"
        )
        handle.write("| Benchmark case | Items / second | Mean real time (ns) | Mean CPU time (ns) |\n")
        handle.write("|---|---:|---:|---:|\n")
        for row in sorted(rows, key=lambda item: str(item["name"])):
            handle.write(
                f"| {row['name']} | {float(row['items_per_second']):.3f} "
                f"| {float(row['real_time']):.3f} "
                f"| {float(row['cpu_time']):.3f} |\n"
            )
        handle.write("\n## Figures\n\n")
        handle.write(f"- {overview.name}: all available benchmark cases.\n")
        if scaling:
            handle.write(f"- {scaling.name}: selected operation workloads as batch size changes.\n")

    print(f"Loaded {len(rows)} mean benchmark records from: {csv_path}")
    print(f"Analysis report: {output_dir / 'summary.md'}")
    print(f"Plot: {overview}")
    if scaling:
        print(f"Plot: {scaling}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        raise SystemExit(f"Error: {error}")
