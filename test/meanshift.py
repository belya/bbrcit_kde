# Add file
# Run process
# Get result
# Do rolling window and get local maximas 
# These are cluster centers, voila!

import sys
import os
import datetime
import tempfile
import subprocess
import pandas as pd

if __name__ == '__main__':

    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', type=str, default='kde_in.csv',
                        help='Sample points to form the kde. ')
    parser.add_argument('--output', type=str, default='kde_scan.csv',
                        help='File name to save scan. ')
    args = parser.parse_args()

    values_df = pd.read_csv(args.input, sep=" ", names=["address", "value"])
    values_df[["value"]].to_csv("./file.csv", sep=" ", header=False)

    subprocess.check_call(['./kde_1d'])

    kde_values_df = pd.read_csv("result.csv", sep=" ", names=["index", "kde"]).set_index("index")
    values_df = values_df.merge(kde_values_df, left_index=True, right_index=True)

    first_max_value = values_df["value"].iloc[0:window_size].max()
    values_df["cluster"] = values_df["kde"].rolling(window_size).max().fillna(first_max_value)
    values_df.to_csv(args.output, sep=" ", header=False)



