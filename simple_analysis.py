#!/usr/bin/env python3
import pandas as pd
import numpy as np
from pathlib import Path
import argparse
import matplotlib.pyplot as plt


def analyze_webrtc_metrics(folder_name):
    """
    Analyze WebRTC metrics from given folder containing frame_metrics.csv and average_stats.csv
    """
    # Construct file paths
    base_path = Path(folder_name)
    frame_metrics_path = base_path / "receiver" / "frame_metrics.csv"
    avg_stats_path = base_path / "receiver" / "average_stats.csv"

    # Check if files exist
    if not frame_metrics_path.exists():
        raise FileNotFoundError(f"Frame metrics file not found at {frame_metrics_path}")
    if not avg_stats_path.exists():
        raise FileNotFoundError(f"Average stats file not found at {avg_stats_path}")

    # Read CSVs
    frame_df = pd.read_csv(frame_metrics_path)
    stats_df = pd.read_csv(avg_stats_path)

    # Overall average bitrate stats
    bitrate_stats = {
        'mean'  : stats_df['overall_avg_bitrates'].mean(),
        'median': stats_df['overall_avg_bitrates'].median(),
        '95th'  : np.percentile(stats_df['overall_avg_bitrates'], 95),
        '99th'  : np.percentile(stats_df['overall_avg_bitrates'], 99),
        '99.9th': np.percentile(stats_df['overall_avg_bitrates'], 99.9)
    }

    # Estimated network ms stats
    network_stats = {
        'mean'  : frame_df['estimated_network_ms'].mean(),
        'median': frame_df['estimated_network_ms'].median(),
        '95th'  : np.percentile(frame_df['estimated_network_ms'], 95),
        '99th'  : np.percentile(frame_df['estimated_network_ms'], 99),
        '99.9th': np.percentile(frame_df['estimated_network_ms'], 99.9)
    }

    # === NEW: framerate stats (p0.1, p01, p05) ===
    framerate = stats_df['framerate'].dropna()

    framerate_stats = {
        'mean'  : framerate.mean(),
        'median': framerate.median(),
        'p0.1'  : np.percentile(framerate, 0.1),
        'p01'   : np.percentile(framerate, 1),
        'p05'   : np.percentile(framerate, 5)
    }

    # Frame-level jitter stats
    frame_df['rtp_ms'] = frame_df['rtp_timestamp'] / 90
    frame_df['departure_time'] = frame_df['rtp_ms'] + frame_df['encode_ms']
    frame_df['departure_diff'] = frame_df['departure_time'].diff()
    frame_df['jitter'] = frame_df['inter_frame_delay_ms'] - frame_df['departure_diff']
    jitter_clean = frame_df['jitter'].replace([np.inf, -np.inf], np.nan).dropna()

    if len(jitter_clean) > 0:
        jitter_stats = {
            'mean'  : jitter_clean.mean(),
            'median': jitter_clean.median(),
            '95th'  : np.percentile(jitter_clean, 95),
            '99th'  : np.percentile(jitter_clean, 99),
            '99.9th': np.percentile(jitter_clean, 99.9)
        }
    else:
        jitter_stats = {k: np.nan for k in ['mean', 'median', '95th', '99th', '99.9th']}

    # Display
    print(f"\nTotal frames: {len(frame_df)}")
    print(f"Valid jitter measurements: {len(jitter_clean)}")

    print(f"\nAnalysis Results for {folder_name}:")
    print("-" * 50)

    print("\nOverall Average Bitrate Statistics (bps):")
    for k, v in bitrate_stats.items():
        print(f"{k.ljust(10)}: {v:,.2f}")

    print("\nEstimated Network MS Statistics:")
    for k, v in network_stats.items():
        print(f"{k.ljust(10)}: {v:.2f}")

    print("\nFramerate Statistics (fps):")
    for k, v in framerate_stats.items():
        print(f"{k.ljust(10)}: {v:.2f}")

    print("\nFrame-level Jitter Statistics (ms):")
    for k, v in jitter_stats.items():
        print(f"{k.ljust(10)}: {v:.2f}")

    
    df_plot = frame_df.copy()
    df_plot['relative_time_s'] = (df_plot['timestamp'] - df_plot['timestamp'].min()) / 1000.0

    plt.figure(figsize=(8, 5))
    plt.plot(df_plot['relative_time_s'], df_plot['estimated_network_ms'], marker='o')
    plt.xlabel("Time (s)")
    plt.ylabel("Estimated Network Delay (ms)")
    plt.title("Estimated Network Delay vs Time")
    plt.grid(True)
    plt.tight_layout()
    plt.show()


def main():
    parser = argparse.ArgumentParser(description='Analyze WebRTC metrics from log files.')
    parser.add_argument('folder', help='Path to the WebRTC logs folder')
    args = parser.parse_args()

    try:
        analyze_webrtc_metrics(args.folder)
    except Exception as e:
        print(f"Error: {e}")
        exit(1)

if __name__ == "__main__":
    main()
