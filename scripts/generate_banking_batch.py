#!/usr/bin/env python3
"""
Batch Banking Dataset Generator

Generates 4 banking datasets in parallel at scales: 1M, 2M, 5M, and 10M accounts.

Usage:
    python3 scripts/generate_banking_batch.py --output-base input/plaintext/banking_scaled
    python3 scripts/generate_banking_batch.py --output-base input/plaintext/banking_scaled --workers 4
"""

import argparse
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

# Dataset configurations: (num_accounts, directory_suffix)
DATASETS = [
    (1_000_000, "1M"),
    (2_000_000, "2M"),
    (5_000_000, "5M"),
    (10_000_000, "10M"),
]


def generate_dataset(num_accounts: int, output_dir: Path) -> tuple:
    """Generate a single dataset using the scaled generator."""
    start_time = time.time()

    script_path = Path(__file__).parent / "generate_banking_scaled.py"

    cmd = [
        sys.executable,
        str(script_path),
        str(num_accounts),
        str(output_dir),
        "--streaming",
        "--quiet",
    ]

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=True,
        )
        elapsed = time.time() - start_time
        return (num_accounts, output_dir, True, elapsed, result.stdout)
    except subprocess.CalledProcessError as e:
        elapsed = time.time() - start_time
        return (num_accounts, output_dir, False, elapsed, e.stderr)


def format_size(num_accounts: int) -> str:
    """Format account count as human-readable string."""
    if num_accounts >= 1_000_000:
        return f"{num_accounts // 1_000_000}M"
    elif num_accounts >= 1_000:
        return f"{num_accounts // 1_000}K"
    return str(num_accounts)


def main():
    parser = argparse.ArgumentParser(
        description="Generate banking datasets at multiple scales in parallel"
    )
    parser.add_argument(
        "--output-base",
        type=Path,
        default=Path("input/plaintext/banking_scaled"),
        help="Base output directory (default: input/plaintext/banking_scaled)",
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=4,
        help="Number of parallel workers (default: 4)",
    )
    args = parser.parse_args()

    output_base = args.output_base
    workers = args.workers

    # Create base output directory
    output_base.mkdir(parents=True, exist_ok=True)

    print("=" * 70)
    print("Batch Banking Dataset Generator")
    print("=" * 70)
    print(f"Output base: {output_base.absolute()}")
    print(f"Workers: {workers}")
    print(f"Datasets to generate: {len(DATASETS)}")
    print()

    # Print dataset info
    print("Dataset specifications:")
    print("-" * 70)
    print(f"{'Dataset':<15} {'Accounts':>12} {'Owners':>12} {'Transactions':>15}")
    print("-" * 70)
    for num_accounts, suffix in DATASETS:
        num_owners = num_accounts // 5
        num_txns = num_accounts * 5
        print(f"banking_{suffix:<10} {num_accounts:>12,} {num_owners:>12,} {num_txns:>15,}")
    print("-" * 70)
    print()

    # Prepare tasks
    tasks = []
    for num_accounts, suffix in DATASETS:
        output_dir = output_base / f"banking_{suffix}"
        tasks.append((num_accounts, output_dir))

    # Execute in parallel
    print(f"Starting generation with {workers} workers...")
    print()

    start_time = time.time()
    results = []

    with ProcessPoolExecutor(max_workers=workers) as executor:
        futures = {
            executor.submit(generate_dataset, num_accounts, output_dir): (num_accounts, output_dir)
            for num_accounts, output_dir in tasks
        }

        for future in as_completed(futures):
            num_accounts, output_dir, success, elapsed, output = future.result()
            size_str = format_size(num_accounts)

            if success:
                print(f"[DONE] banking_{size_str}: {elapsed:.1f}s")
            else:
                print(f"[FAIL] banking_{size_str}: {output}")

            results.append((num_accounts, output_dir, success, elapsed))

    total_time = time.time() - start_time

    # Summary
    print()
    print("=" * 70)
    print("Generation Summary")
    print("=" * 70)

    successful = sum(1 for r in results if r[2])
    failed = len(results) - successful

    print(f"Total time: {total_time:.1f}s")
    print(f"Successful: {successful}/{len(results)}")
    if failed > 0:
        print(f"Failed: {failed}")

    print()
    print("Generated datasets:")
    for num_accounts, output_dir, success, elapsed in sorted(results):
        status = "OK" if success else "FAILED"
        size_str = format_size(num_accounts)
        print(f"  [{status}] {output_dir} ({size_str} accounts, {elapsed:.1f}s)")

    print()
    print("=" * 70)

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
