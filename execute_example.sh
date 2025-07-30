#!/bin/bash

# Check if required arguments (room_id and is_sender) are provided
if [ -z "$1" ] || [ -z "$2" ]; then
    echo "Usage: $0 <room_id> <is_sender> [y4m_path]"
    echo "  room_id: Room identifier"
    echo "  is_sender: true or false"
    echo "  y4m_path: Path to y4m file (optional)"
    exit 1
fi

# Assign the arguments to variables
room_id="$1"
is_sender="$2"
y4m_path="$3"

# Validate is_sender argument
if [ "$is_sender" != "true" ] && [ "$is_sender" != "false" ]; then
    echo "Error: is_sender must be 'true' or 'false'"
    exit 1
fi

# If y4m_path is provided, verify it exists
if [ ! -z "$y4m_path" ] && [ ! -f "$y4m_path" ]; then
    echo "Error: y4m file does not exist: $y4m_path"
    exit 1
fi

# Set headless based on is_sender
if [ "$is_sender" = "true" ]; then
    headless="true"
else
    headless="false"
fi

# Build the command
cmd="./peerconnection_client --server=goodsol.overlinkapp.org --is_sender=$is_sender --room_id=$room_id --headless=$headless"

# Add y4m_path to command only if it's provided
if [ ! -z "$y4m_path" ]; then
    cmd="$cmd --y4m_path=$y4m_path"
fi

pulseaudio --start
# Execute the command
$cmd