#!/bin/bash

# Check if room_id is provided as an argument
if [ -z "$1" ]; then
    echo "Usage: $0 <room_id>"
    exit 1
fi

# Assign the first argument to room_id
room_id="$1"

# Run the peerconnection_client command with the provided room_id
./out/Release/peerconnection_client --server=goodsol.overlinkapp.org --headless=false --is_sender=false --room_id="$room_id"