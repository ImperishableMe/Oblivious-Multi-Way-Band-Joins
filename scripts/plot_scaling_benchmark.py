#!/usr/bin/env python3
"""
Plot Scaling Benchmark Results

Usage: python3 plot_scaling_benchmark.py <results.csv> <output.png>

Example:
    python3 scripts/plot_scaling_benchmark.py output/scaling_results_banking_chain4_filtered.csv output/scaling_chain4.png
"""

import csv
import sys
from pathlib import Path

try:
    import matplotlib.pyplot as plt
    import matplotlib.ticker as ticker
except ImportError:
    print("Error: matplotlib is required. Install with: pip install matplotlib")
    sys.exit(1)


def read_results(csv_path):
    """Read benchmark results from CSV file."""
    results = {
        'accounts': [],
        'non_decomposed': [],
        'decomposed': [],
        'onehop': [],
        'mwbj': [],
        'speedup': []
    }

    with open(csv_path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            results['accounts'].append(int(row['accounts']))
            results['non_decomposed'].append(float(row['non_decomposed']))
            results['decomposed'].append(float(row['decomposed']))
            results['onehop'].append(float(row['onehop']))
            results['mwbj'].append(float(row['mwbj']))
            results['speedup'].append(float(row['speedup']))

    return results


def plot_results(results, output_path, title=None):
    """Generate line plot comparing approaches."""
    # Create figure
    fig, ax = plt.subplots(figsize=(10, 6))

    accounts = results['accounts']

    # Plot lines
    ax.plot(accounts, results['non_decomposed'], 'o-', color='#d62728',
            linewidth=2, markersize=8, label='Non-Decomposed')
    ax.plot(accounts, results['decomposed'], 's-', color='#1f77b4',
            linewidth=2, markersize=8, label='Decomposed')

    # Labels and title
    ax.set_xlabel('Number of Accounts', fontsize=12)
    ax.set_ylabel('Query Execution Time (seconds)', fontsize=12)

    if title:
        ax.set_title(title, fontsize=14)
    else:
        ax.set_title('Scaling Comparison: Non-Decomposed vs Decomposed', fontsize=14)

    # Format x-axis with K suffix
    def format_accounts(x, pos):
        if x >= 1000:
            return f'{int(x/1000)}K'
        return str(int(x))

    ax.xaxis.set_major_formatter(ticker.FuncFormatter(format_accounts))

    # Set x-axis ticks to match data points
    ax.set_xticks(accounts)

    # Grid
    ax.grid(True, linestyle='--', alpha=0.7)

    # Legend
    ax.legend(loc='upper left', fontsize=11)

    # Add speedup annotations
    for i, (x, y_non, y_dec, speedup) in enumerate(zip(
            accounts, results['non_decomposed'], results['decomposed'], results['speedup'])):
        # Add speedup text near decomposed line
        ax.annotate(f'{speedup:.2f}x',
                    xy=(x, y_dec),
                    xytext=(5, -15),
                    textcoords='offset points',
                    fontsize=9,
                    color='#1f77b4')

    # Tight layout
    plt.tight_layout()

    # Save figure
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"Plot saved to: {output_path}")

    # Also show if running interactively
    # plt.show()


def main():
    if len(sys.argv) < 3:
        print("Usage: python3 plot_scaling_benchmark.py <results.csv> <output.png>")
        print("Example: python3 scripts/plot_scaling_benchmark.py output/scaling_results_banking_chain4_filtered.csv output/scaling_chain4.png")
        sys.exit(1)

    csv_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])

    if not csv_path.exists():
        print(f"Error: Results file not found: {csv_path}")
        sys.exit(1)

    # Extract query name from CSV filename for title
    query_name = csv_path.stem.replace('scaling_results_', '').replace('_', ' ').title()
    title = f'Scaling Comparison: {query_name}'

    # Read and plot results
    results = read_results(csv_path)
    plot_results(results, output_path, title)


if __name__ == '__main__':
    main()
