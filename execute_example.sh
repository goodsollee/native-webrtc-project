#!/usr/bin/env bash
set -euo pipefail

################################################################################
# Usage & defaults
################################################################################
usage() {
    echo "Usage: $0 <room_id> <is_sender> [--min N] [--max N] [--y4m PATH]"
    echo "  room_id : Room identifier"
    echo "  is_sender : true | false"
    echo "  --min N   : (optional) playout‑delay min in ms   [default: 0]"
    echo "  --max N   : (optional) playout‑delay max in ms   [default: 150]"
    echo "  --y4m PATH: (optional) path to *.y4m file to send"
    exit 1
}

# hard defaults
min_delay=0
max_delay=10
y4m_path=""

################################################################################
# Positional args (room_id, is_sender) + option parsing
################################################################################
positional=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --min)  min_delay="$2"; shift 2 ;;
        --max)  max_delay="$2"; shift 2 ;;
        --y4m)  y4m_path="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) positional+=("$1"); shift ;;
    esac
done
set -- "${positional[@]}"

[[ ${#positional[@]} -lt 2 ]] && usage
room_id="$1"
is_sender="$2"

################################################################################
# Simple validation
################################################################################
[[ "$is_sender" != "true" && "$is_sender" != "false" ]] && {
    echo "Error: is_sender must be 'true' or 'false'"; exit 1; }

[[ "$min_delay" =~ ^[0-9]+$ && "$max_delay" =~ ^[0-9]+$ ]] || {
    echo "Error: --min and --max must be integers"; exit 1; }

(( min_delay <= max_delay )) || {
    echo "Error: --min must be ≤ --max"; exit 1; }

[[ -n "$y4m_path" && ! -f "$y4m_path" ]] && {
    echo "Error: y4m file does not exist: $y4m_path"; exit 1; }

################################################################################
# Build and run peerconnection_client
################################################################################
headless=$([[ "$is_sender" == "true" ]] && echo "true" || echo "false")

cmd="./out/Default/peerconnection_client \
      --server=goodsol.overlinkapp.org \
      --is_sender=${is_sender} \
      --room_id=${room_id} \
      --headless=${headless} \
      --force_fieldtrials=\"WebRTC-ForcePlayoutDelay/min_ms:${min_delay},max_ms:${max_delay}/\""

[[ -n "$y4m_path" ]] && cmd+=" --y4m_path=${y4m_path}"

pulseaudio --start
eval "${cmd}"
