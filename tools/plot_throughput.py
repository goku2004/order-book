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
        parsed_row: dict[str, object] = {
            "name": mean_name,
            "items_per_second": rate,
            "real_time": float(row["real_time"]),
            "cpu_time": float(row["cpu_time"]),
            "time_unit": row.get("time_unit", ""),
        }

        # Preserve custom Google Benchmark counters when present. They are useful
        # for replay category rates and execution rates, not just API-call rate.
        optional_numeric_fields = (
            "trade_events_per_second",
            "executed_quantity_per_second",
            "passive_inserts_per_second",
            "single_level_match_ops_per_second",
            "sweep_ops_per_second",
            "sparse_ops_per_second",
            "trade_events_per_api_call",
            "workload_profile_id",
            "workload_seed",
        )
        for field in optional_numeric_fields:
            raw_value = row.get(field, "")
            if not raw_value:
                continue
            try:
                numeric_value = float(raw_value)
            except ValueError:
                continue
            if math.isfinite(numeric_value):
                parsed_row[field] = numeric_value

        parsed.append(parsed_row)

    if not parsed:
        raise ValueError("No valid mean rows with positive items_per_second found")
    return parsed


def display_benchmark_name(name: str) -> str:
    seeded = re.match(r"BM_SeededMixedReplay/(\d+)/(\d+)/(\d+)", name)
    if seeded:
        operation_count = int(seeded.group(1))
        seed = int(seeded.group(2))
        profile_id = int(seeded.group(3))
        profile_names = {
            0: "Balanced 45/25/20/10",
            1: "Insert-heavy 60/25/10/5",
            2: "Match-heavy 25/40/25/10",
        }
        profile = profile_names.get(profile_id, f"Unknown profile {profile_id}")
        return f"{profile} | {operation_count:,} ops | seed 0x{seed:X}"

    return name.replace("BM_", "").replace("_", " ")


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
    labels = [display_benchmark_name(str(row["name"])) for row in ordered]
    values = [float(row["items_per_second"]) for row in ordered]

    fig, ax = plt.subplots(figsize=(12, max(7, len(rows) * 0.34)))
    positions = list(range(len(ordered)))
    # A dot plot is preferable to bars on a logarithmic axis: bars cannot
    # have a meaningful zero baseline on a log scale.
    ax.scatter(values, positions, s=38)
    ax.set_yticks(positions, labels)
    ax.set_ylim(-0.5, len(ordered) - 0.5)
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


def plot_seeded_profile_rates(
    rows: list[dict[str, object]],
    output_dir: Path,
) -> Path | None:
    import matplotlib.pyplot as plt

    seeded_rows = []
    for row in rows:
        match = re.match(
            r"BM_SeededMixedReplay/(\d+)/(\d+)/(\d+)",
            str(row["name"]),
        )
        if not match or int(match.group(1)) != 4_000:
            continue
        seeded_rows.append(row)

    if not seeded_rows:
        return None

    # The graph shows each category's contribution to total calls/second under
    # that workload mix. These are not isolated per-operation service rates.
    fields = (
        ("passive_inserts_per_second", "Passive inserts"),
        ("single_level_match_ops_per_second", "Single-level matches"),
        ("sweep_ops_per_second", "Multi-level sweeps"),
        ("sparse_ops_per_second", "Sparse operations"),
    )
    if not any(any(field in row for row in seeded_rows) for field, _ in fields):
        return None

    labels = [display_benchmark_name(str(row["name"])) for row in seeded_rows]
    x = list(range(len(seeded_rows)))
    width = 0.8 / len(fields)
    fig, ax = plt.subplots(figsize=(12, 6.5))

    plotted = False
    for series_index, (field, label) in enumerate(fields):
        values = [
            float(row.get(field, 0.0))
            for row in seeded_rows
        ]
        if not any(value > 0 for value in values):
            continue
        offsets = [
            index - 0.4 + width / 2 + series_index * width
            for index in x
        ]
        ax.bar(offsets, values, width=width, label=label)
        plotted = True

    if not plotted:
        plt.close(fig)
        return None

    ax.set_xticks(x, labels, rotation=22, ha="right")
    ax.set_ylabel("Category operations per second")
    ax.set_title("Seeded replay: category rates under different operation mixes")
    ax.grid(axis="y", alpha=0.25)
    ax.legend()
    fig.tight_layout()

    path = output_dir / "seeded_profile_category_rates.png"
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
    profile_rates = plot_seeded_profile_rates(rows, output_dir)

    with (output_dir / "summary.md").open("w", encoding="utf-8") as handle:
        handle.write("# Throughput benchmark report\n\n")
        handle.write(f"Input CSV: {csv_path}\n\n")
        handle.write(
            "This report includes only mean aggregates with positive, finite "
            "items_per_second values. The overview compares different workload "
            "types; it should not be interpreted as a fair ranking when workloads "
            "have different amounts of work per operation.\n\n"
        )
        handle.write(
            "| Benchmark case | Items / second | Mean real time (ns) | "
            "Mean CPU time (ns) | Trade events / second | Trades / API call |\n"
        )
        handle.write("|---|---:|---:|---:|---:|---:|\n")
        for row in sorted(rows, key=lambda item: str(item["name"])):
            trade_rate = row.get("trade_events_per_second")
            events_per_call = row.get("trade_events_per_api_call")
            trade_rate_text = (
                f"{float(trade_rate):.3f}" if trade_rate is not None else "—"
            )
            events_per_call_text = (
                f"{float(events_per_call):.4f}"
                if events_per_call is not None else "—"
            )
            handle.write(
                f"| {display_benchmark_name(str(row['name']))} "
                f"| {float(row['items_per_second']):.3f} "
                f"| {float(row['real_time']):.3f} "
                f"| {float(row['cpu_time']):.3f} "
                f"| {trade_rate_text} | {events_per_call_text} |\n"
            )
        handle.write("\n## Figures\n\n")
        handle.write(f"- {overview.name}: all available benchmark cases.\n")
        if scaling:
            handle.write(f"- {scaling.name}: selected operation workloads as batch size changes.\n")
        if profile_rates:
            handle.write(
                f"- {profile_rates.name}: category rates for 4,000-operation seeded profiles.\n"
            )

    print(f"Loaded {len(rows)} mean benchmark records from: {csv_path}")
    print(f"Analysis report: {output_dir / 'summary.md'}")
    print(f"Plot: {overview}")
    if scaling:
        print(f"Plot: {scaling}")
    if profile_rates:
        print(f"Plot: {profile_rates}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        raise SystemExit(f"Error: {error}")
