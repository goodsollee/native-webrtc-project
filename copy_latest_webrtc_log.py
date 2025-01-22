#!/usr/bin/env python3

import os
import shutil
import argparse
from datetime import datetime

def copy_latest_folder(source_dir, target_dir, new_name):
    # Get list of all folders in source directory
    folders = [f for f in os.listdir(source_dir) if os.path.isdir(os.path.join(source_dir, f))]
    
    if not folders:
        print(f"No folders found in {source_dir}")
        return False
    
    # Sort folders by name (since they're datetime strings, this will sort chronologically)
    # In this case, format is YYYY-MM-DD_HH-MM-SS_name
    latest_folder = sorted(folders)[-1]
    
    # Create target directory if it doesn't exist
    os.makedirs(target_dir, exist_ok=True)
    
    # Full paths
    source_path = os.path.join(source_dir, latest_folder)
    target_path = os.path.join(target_dir, new_name)
    
    # Remove target if it already exists
    if os.path.exists(target_path):
        shutil.rmtree(target_path)
    
    # Copy the folder
    try:
        shutil.copytree(source_path, target_path)
        print(f"Successfully copied {latest_folder} to {target_path}")
        return True
    except Exception as e:
        print(f"Error copying folder: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Copy latest WebRTC log folder to a new location with a new name')
    parser.add_argument('--target_dir', type=str, required=True,
                        help='Target directory path')
    parser.add_argument('--new_name', type=str, required=True,
                        help='New name for the copied folder')
    
    args = parser.parse_args()
    
    # Source directory is fixed relative to the script location
    script_dir = os.path.dirname(os.path.abspath(__file__))
    source_dir = os.path.join(script_dir, 'webrtc_logs')
    
    copy_latest_folder(source_dir, args.target_dir, args.new_name)

if __name__ == '__main__':
    main()