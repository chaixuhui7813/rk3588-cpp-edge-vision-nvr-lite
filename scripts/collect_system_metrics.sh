#!/usr/bin/env bash
set -euo pipefail

INTERVAL="${INTERVAL:-1}"
OUT="${1:-metrics.csv}"
PID_FILTER="${PID_FILTER:-edge_vision_nvr}"

mkdir -p "$(dirname "$OUT")"

echo "timestamp,epoch,cpu_percent,mem_used_mb,mem_available_mb,process_rss_mb,npu_load_percent,npu_cur_freq_hz,npu_governor" > "$OUT"

detect_npu_devfreq() {
  for path in /sys/class/devfreq/*npu* /sys/class/devfreq/*fdab0000*; do
    if [ -d "$path" ] && [ -f "$path/load" ]; then
      echo "$path"
      return 0
    fi
  done
  return 1
}

read_cpu_line() {
  awk '/^cpu / {print $2" "$3" "$4" "$5" "$6" "$7" "$8" "$9" "$10}' /proc/stat
}

cpu_percent() {
  local prev=($1)
  local curr=($2)
  local prev_idle=$((prev[3] + prev[4]))
  local curr_idle=$((curr[3] + curr[4]))
  local prev_total=0
  local curr_total=0
  local v
  for v in "${prev[@]}"; do prev_total=$((prev_total + v)); done
  for v in "${curr[@]}"; do curr_total=$((curr_total + v)); done
  local total_diff=$((curr_total - prev_total))
  local idle_diff=$((curr_idle - prev_idle))
  if [ "$total_diff" -le 0 ]; then
    echo "0.00"
  else
    awk -v t="$total_diff" -v i="$idle_diff" 'BEGIN { printf "%.2f", (t - i) * 100.0 / t }'
  fi
}

memory_field_mb() {
  local key="$1"
  awk -v key="$key" '$1 == key":" { printf "%.2f", $2 / 1024.0 }' /proc/meminfo
}

process_rss_mb() {
  local pid
  pid="$(pgrep -n -f "$PID_FILTER" || true)"
  if [ -z "$pid" ] || [ ! -r "/proc/$pid/status" ]; then
    echo "0.00"
    return
  fi
  awk '$1 == "VmRSS:" { printf "%.2f", $2 / 1024.0; found=1 } END { if (!found) printf "0.00" }' "/proc/$pid/status"
}

NPU_PATH="$(detect_npu_devfreq || true)"
prev_cpu="$(read_cpu_line)"
sleep "$INTERVAL"

while true; do
  now_iso="$(date -Iseconds)"
  now_epoch="$(date +%s)"
  curr_cpu="$(read_cpu_line)"
  cpu="$(cpu_percent "$prev_cpu" "$curr_cpu")"
  prev_cpu="$curr_cpu"

  mem_total="$(memory_field_mb MemTotal)"
  mem_avail="$(memory_field_mb MemAvailable)"
  mem_used="$(awk -v total="$mem_total" -v avail="$mem_avail" 'BEGIN { printf "%.2f", total - avail }')"
  rss="$(process_rss_mb)"

  npu_load="0"
  npu_freq="0"
  npu_governor=""
  if [ -n "$NPU_PATH" ]; then
    raw_load="$(cat "$NPU_PATH/load" 2>/dev/null || true)"
    npu_load="$(printf "%s" "$raw_load" | sed -n 's/^\([0-9]\+\)@.*/\1/p')"
    npu_load="${npu_load:-0}"
    npu_freq="$(cat "$NPU_PATH/cur_freq" 2>/dev/null || echo 0)"
    npu_governor="$(cat "$NPU_PATH/governor" 2>/dev/null || true)"
  fi

  echo "$now_iso,$now_epoch,$cpu,$mem_used,$mem_avail,$rss,$npu_load,$npu_freq,$npu_governor" >> "$OUT"
  sleep "$INTERVAL"
done
