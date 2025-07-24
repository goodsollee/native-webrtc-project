import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# Read the CSV file
df = pd.read_csv('webrtc_logs/2025-05-30_22-00-35_test54/receiver/frame_metrics.csv')

# Filter data where first_packet_departure > 0
df_filtered = df[df['first_packet_departure'] > 0].copy()

# Reset index to get frame indices starting from 0
df_filtered.reset_index(drop=True, inplace=True)

# Start from index 2 onwards
df_filtered = df_filtered.iloc[3:].copy()
df_filtered.reset_index(drop=True, inplace=True)

# Set periodicity
periodicity = 33

print(f"Set periodicity: {periodicity:.2f} ms")
print(f"Number of frames after filtering: {len(df_filtered)}")

# Use the FIRST ARRIVAL time as the baseline (not departure)
first_arrival = df_filtered['first_packet_arrival'].iloc[0]
frame_indices = np.arange(len(df_filtered))

# Expected arrival times: first_arrival + periodicity * frame_index
expected_arrivals = first_arrival + (periodicity * frame_indices)

# Actual arrival times
actual_arrivals = df_filtered['first_packet_arrival'].values

# Calculate the difference (actual - expected)
raw_differences = actual_arrivals - expected_arrivals
timing_differences = ((raw_differences + periodicity) % periodicity*2) - periodicity

# For frame 0, the difference should be exactly 0
timing_differences[0] = 0

# Create the plot
plt.figure(figsize=(12, 8))
plt.plot(frame_indices, timing_differences, 'b-', linewidth=1, alpha=0.7, label='Timing Difference')
plt.scatter(frame_indices, timing_differences, c='red', s=20, alpha=0.6)

plt.axhline(y=0, color='black', linestyle='--', alpha=0.5, label='Perfect Timing')
plt.xlabel('Frame Index')
plt.ylabel('Timing Difference (ms)')
plt.title('Frame Arrival Timing Difference vs Expected\n(Actual Arrival - Expected Arrival)')
plt.grid(True, alpha=0.3)
plt.legend()
plt.xlim([0,200])

# Add statistics to the plot
mean_diff = np.mean(timing_differences)
std_diff = np.std(timing_differences)
plt.text(0.02, 0.98, f'Mean difference: {mean_diff:.2f} ms\nStd deviation: {std_diff:.2f} ms', 
         transform=plt.gca().transAxes, verticalalignment='top',
         bbox=dict(boxstyle='round', facecolor='white', alpha=0.8))

plt.tight_layout()
plt.show()

# Print detailed statistics
print(f"\nTiming Analysis Results:")
print(f"Mean timing difference: {mean_diff:.2f} ms")
print(f"Standard deviation: {std_diff:.2f} ms")
print(f"Min difference: {np.min(timing_differences):.2f} ms")
print(f"Max difference: {np.max(timing_differences):.2f} ms")
print(f"Median difference: {np.median(timing_differences):.2f} ms")

# Show histogram
plt.figure(figsize=(10, 6))
plt.hist(timing_differences, bins=20, alpha=0.7, edgecolor='black')
plt.xlabel('Timing Difference (ms)')
plt.ylabel('Frequency')
plt.title('Distribution of Timing Differences')
plt.axvline(x=mean_diff, color='red', linestyle='--', label=f'Mean: {mean_diff:.2f} ms')
plt.grid(True, alpha=0.3)
plt.legend()
plt.tight_layout()
plt.show()

# Create detailed dataframe
analysis_df = pd.DataFrame({
    'frame_index': frame_indices,
    'first_packet_departure': df_filtered['first_packet_departure'].values,
    'first_packet_arrival': actual_arrivals,
    'expected_arrival': expected_arrivals,
    'timing_difference': timing_differences
})

# Display first few rows
print(f"\nFirst 10 rows of analysis:")
print(analysis_df.head(10).round(2))

# Verify frame 0 is now 0
print(f"\nFrame 0 timing difference: {timing_differences[0]:.2f} ms")

# Save results
analysis_df.to_csv('timing_analysis_results.csv', index=False)