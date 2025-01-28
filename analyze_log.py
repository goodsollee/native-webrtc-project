import pandas as pd
import numpy as np
import sys
import matplotlib.pyplot as plt

def analyze_and_plot_column(file_name, column_name):
    try:
        # Read CSV file
        df = pd.read_csv(file_name)
        
        # Check if column exists
        if column_name not in df.columns:
            print(f"Error: Column '{column_name}' not found in the file.")
            print(f"Available columns are: {', '.join(df.columns)}")
            return
        
        # Calculate statistics
        stats = {
            'mean': df[column_name].mean(),
            'std': df[column_name].std(),
            'median': df[column_name].median(),
            '0.1th_percentile': np.percentile(df[column_name], 0.1),
            '1st_percentile': np.percentile(df[column_name], 1),
            '5th_percentile': np.percentile(df[column_name], 5),
            '95th_percentile': np.percentile(df[column_name], 95),
            '99th_percentile': np.percentile(df[column_name], 99),
            '99.9th_percentile': np.percentile(df[column_name], 99.9)
        }
        
        # Print results
        print(f"\nStatistics for column: {column_name}")
        print("-" * 50)
        print(f"Mean: {stats['mean']:.3f}")
        print(f"Standard Deviation: {stats['std']:.3f}")
        print(f"Median: {stats['median']:.3f}")
        print("\nPercentiles:")
        print(f"0.1th Percentile: {stats['0.1th_percentile']:.3f}")
        print(f"1st Percentile: {stats['1st_percentile']:.3f}")
        print(f"5th Percentile: {stats['5th_percentile']:.3f}")
        print(f"95th Percentile: {stats['95th_percentile']:.3f}")
        print(f"99th Percentile: {stats['99th_percentile']:.3f}")
        print(f"99.9th Percentile: {stats['99.9th_percentile']:.3f}")
        
        # Create visualization
        plt.figure(figsize=(12, 6))
        
        # Plot time series
        relative_time = (df['timestamp'] - df['timestamp'].iloc[0])
        plt.subplot(1, 2, 1)
        plt.plot(relative_time, df[column_name], 'b-', label=column_name)
        plt.axhline(y=stats['mean'], color='r', linestyle='--', label='Mean')
        plt.fill_between(relative_time,
                        stats['mean'] - stats['std'],
                        stats['mean'] + stats['std'],
                        alpha=0.2,
                        color='r',
                        label='±1 STD')
        plt.xlabel('Time (ms)')
        plt.ylabel(column_name)
        plt.title(f'{column_name} Over Time')
        plt.legend()
        plt.grid(True)
        
        # Plot histogram
        plt.subplot(1, 2, 2)
        plt.hist(df[column_name], bins=30, alpha=0.7, color='b')
        plt.axvline(x=stats['mean'], color='r', linestyle='--', label='Mean')
        plt.axvline(x=stats['median'], color='g', linestyle='--', label='Median')
        plt.axvline(x=stats['95th_percentile'], color='y', linestyle='--', label='95th Percentile')
        plt.xlabel(column_name)
        plt.ylabel('Frequency')
        plt.title(f'Distribution of {column_name}')
        plt.legend()
        plt.grid(True)
        
        plt.tight_layout()
        plt.show()
        
    except FileNotFoundError:
        print(f"Error: File '{file_name}' not found.")
    except Exception as e:
        print(f"Error: {str(e)}")

def main():
    if len(sys.argv) != 3:
        print("Usage: python script.py <file_name> <column_name>")
        print("Example: python script.py logfile.csv encode_ms")
        return
    
    file_name = sys.argv[1]
    column_name = sys.argv[2]
    analyze_and_plot_column(file_name, column_name)

if __name__ == "__main__":
    main()