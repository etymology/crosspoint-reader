#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="$ROOT_DIR/.pio/test-logs"
mkdir -p "$LOG_DIR"

BASELINE_ENV="display_perf_baseline"
OPTIMIZED_ENV="display_perf_optimized"

BASELINE_LOG="$LOG_DIR/display_perf_baseline.log"
OPTIMIZED_LOG="$LOG_DIR/display_perf_optimized.log"

ALLOW_SLOWDOWN_PERCENT="${ALLOW_SLOWDOWN_PERCENT:-0}"

UPLOAD_ARGS=()
if [[ $# -ge 1 && -n "${1:-}" ]]; then
  PORT="$1"
  UPLOAD_ARGS=(--upload-port "$PORT" --test-port "$PORT")
fi

run_env() {
  local env_name="$1"
  local out_log="$2"
  echo "== Running $env_name =="
  (
    cd "$ROOT_DIR"
    pio test -e "$env_name" "${UPLOAD_ARGS[@]}"
  ) | tee "$out_log"
}

extract_metric() {
  local metric="$1"
  local log_file="$2"
  local value
  value="$(grep -Eo "${metric}=[0-9]+" "$log_file" | tail -n1 | cut -d= -f2 || true)"
  if [[ -z "$value" ]]; then
    echo "Failed to extract ${metric} from $log_file" >&2
    exit 1
  fi
  echo "$value"
}

print_row() {
  local name="$1"
  local base="$2"
  local opt="$3"
  local delta_ms=$((base - opt))
  local delta_pct
  delta_pct="$(awk -v b="$base" -v o="$opt" 'BEGIN { printf "%.2f", ((b-o)*100.0)/b }')"
  printf "%-24s baseline=%4sms optimized=%4sms delta=%4sms (%6s%%)\n" "$name" "$base" "$opt" "$delta_ms" "$delta_pct"
}

check_regression() {
  local name="$1"
  local base="$2"
  local opt="$3"
  local allowed
  allowed="$(awk -v b="$base" -v p="$ALLOW_SLOWDOWN_PERCENT" 'BEGIN { printf "%.0f", b * (100.0 + p) / 100.0 }')"
  if (( opt > allowed )); then
    echo "Regression: ${name} optimized=${opt}ms baseline=${base}ms allowed=${allowed}ms" >&2
    return 1
  fi
  return 0
}

run_env "$BASELINE_ENV" "$BASELINE_LOG"
run_env "$OPTIMIZED_ENV" "$OPTIMIZED_LOG"

base_bw_full="$(extract_metric "bw_full_median_ms" "$BASELINE_LOG")"
base_bw_half="$(extract_metric "bw_half_median_ms" "$BASELINE_LOG")"
base_gray_full="$(extract_metric "gray_full_total_median_ms" "$BASELINE_LOG")"
base_gray_half="$(extract_metric "gray_half_total_median_ms" "$BASELINE_LOG")"

opt_bw_full="$(extract_metric "bw_full_median_ms" "$OPTIMIZED_LOG")"
opt_bw_half="$(extract_metric "bw_half_median_ms" "$OPTIMIZED_LOG")"
opt_gray_full="$(extract_metric "gray_full_total_median_ms" "$OPTIMIZED_LOG")"
opt_gray_half="$(extract_metric "gray_half_total_median_ms" "$OPTIMIZED_LOG")"

echo
echo "== Display Driver A/B Comparison =="
print_row "BW full refresh" "$base_bw_full" "$opt_bw_full"
print_row "BW half refresh" "$base_bw_half" "$opt_bw_half"
print_row "Gray total (full)" "$base_gray_full" "$opt_gray_full"
print_row "Gray total (half)" "$base_gray_half" "$opt_gray_half"
echo "Allowed slowdown: ${ALLOW_SLOWDOWN_PERCENT}%"

check_regression "BW full refresh" "$base_bw_full" "$opt_bw_full"
check_regression "BW half refresh" "$base_bw_half" "$opt_bw_half"
check_regression "Gray total (full)" "$base_gray_full" "$opt_gray_full"
check_regression "Gray total (half)" "$base_gray_half" "$opt_gray_half"

echo "Comparison PASS"
