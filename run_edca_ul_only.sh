#!/bin/bash
START_TIME=$SECONDS

# Configuration: Accept arguments from command line or use defaults
EDCA=${1:-20}
SIMTIME=30.0
APP_START_SPREAD=5.0
MAX_BYTES_PER_FLOW=1048576
USE_RATE_LIMITED_TRAFFIC=true
OFFERED_LOAD_MBPS_PER_FLOW=1.0
SIMTIME_TAG=$(printf "%s" "$SIMTIME" | sed 's/\.0$//; s/\./p/g')
BASE_DIR="results/edca_only_ul_${EDCA}_sim_${SIMTIME_TAG}s"

echo "=========================================================="
echo " Starting EDCA-Only UL Baseline"
echo " Configuration: 0 P-EDCA STAs, $EDCA EDCA STAs, $SIMTIME sec"
echo " Workload: UL=true, DL=false"
echo " Traffic profile: VO_ONLY=true, MAX_BYTES_PER_FLOW=$MAX_BYTES_PER_FLOW, APP_START_SPREAD=$APP_START_SPREAD"
echo " Rate-limited traffic: $USE_RATE_LIMITED_TRAFFIC, OFFERED_LOAD_MBPS_PER_FLOW=$OFFERED_LOAD_MBPS_PER_FLOW"
echo "=========================================================="
echo ""

./ns3 run "scratch/uhr-mld-mixed-pedca --numPedcaStas=0 --numEdcaStas=$EDCA --simTime=$SIMTIME --distributePedcaAcrossLinks=false --enableAnim=false --enableUl=true --enableDl=false --voOnlyTraffic=true --maxBytesPerFlow=$MAX_BYTES_PER_FLOW --appStartSpread=$APP_START_SPREAD --useRateLimitedTraffic=$USE_RATE_LIMITED_TRAFFIC --offeredLoadMbpsPerFlow=$OFFERED_LOAD_MBPS_PER_FLOW"

mkdir -p results
mkdir -p "$BASE_DIR"

SOURCE_DIR="results/pedca_0_edca_${EDCA}_sim_${SIMTIME_TAG}s/Dist0"
SUMMARY_TXT="$BASE_DIR/summary.txt"

get_metric() {
    local file="$1"
    local key="$2"
    awk -F',' -v metric="$key" '$1 == metric { print $2; exit }' "$file"
}

if [[ ! -f "$SOURCE_DIR/tail-summary.csv" ]]; then
    echo "Expected summary file not found: $SOURCE_DIR/tail-summary.csv"
    exit 1
fi

cat > "$SUMMARY_TXT" << EOF
EDCA-Only UL Baseline Summary
============================
Configuration:
  P-EDCA STAs: 0
  EDCA STAs: $EDCA
  Simulation time: $SIMTIME sec
  UL only: true
  DL enabled: false
  VO only traffic: true
  Use rate-limited traffic: $USE_RATE_LIMITED_TRAFFIC
  Offered load per flow (Mbps): $OFFERED_LOAD_MBPS_PER_FLOW
  Max bytes per flow: $MAX_BYTES_PER_FLOW
  App start spread (s): $APP_START_SPREAD
  Source results folder: $SOURCE_DIR

Key metrics:
  Aggregate throughput (Mbit/s): $(get_metric "$SOURCE_DIR/tail-summary.csv" aggregate_throughput_mbps)
  P-EDCA throughput (Mbit/s):    $(get_metric "$SOURCE_DIR/tail-summary.csv" pedca_throughput_mbps)
  EDCA throughput (Mbit/s):      $(get_metric "$SOURCE_DIR/tail-summary.csv" edca_throughput_mbps)
  Jain's Fairness Index:         $(get_metric "$SOURCE_DIR/tail-summary.csv" jfi)
  Delay p95 (ms):                $(get_metric "$SOURCE_DIR/tail-summary.csv" delay_p95_ms)
  Delay p99 (ms):                $(get_metric "$SOURCE_DIR/tail-summary.csv" delay_p99_ms)
  Jitter p95 (ms):               $(get_metric "$SOURCE_DIR/tail-summary.csv" jitter_p95_ms)
  Jitter p99 (ms):               $(get_metric "$SOURCE_DIR/tail-summary.csv" jitter_p99_ms)
  VO delay p95 (ms):             $(get_metric "$SOURCE_DIR/tail-summary.csv" vo_delay_p95_ms)
  VO jitter p95 (ms):            $(get_metric "$SOURCE_DIR/tail-summary.csv" vo_jitter_p95_ms)
  VO MAC data failed:            $(get_metric "$SOURCE_DIR/tail-summary.csv" vo_mac_tx_data_failed)
  VO MAC final data failed:      $(get_metric "$SOURCE_DIR/tail-summary.csv" vo_mac_tx_final_data_failed)
  VO RTS failed:                 $(get_metric "$SOURCE_DIR/tail-summary.csv" vo_mac_tx_rts_failed)
  P-EDCA selections:             $(get_metric "$SOURCE_DIR/tail-summary.csv" pedca_selections)
  P-EDCA CTS attempts:           $(get_metric "$SOURCE_DIR/tail-summary.csv" pedca_cts_attempts)
  P-EDCA successes:              $(get_metric "$SOURCE_DIR/tail-summary.csv" pedca_successes)
  P-EDCA failures:               $(get_metric "$SOURCE_DIR/tail-summary.csv" pedca_failures)
  P-EDCA resets:                 $(get_metric "$SOURCE_DIR/tail-summary.csv" pedca_resets)
EOF

cp "$SOURCE_DIR/tail-summary.csv" "$BASE_DIR/tail-summary.csv"
cp "$SOURCE_DIR/tail-delay-cdf.csv" "$BASE_DIR/tail-delay-cdf.csv"
cp "$SOURCE_DIR/tail-jitter-cdf.csv" "$BASE_DIR/tail-jitter-cdf.csv"
cp "$SOURCE_DIR/tail-completion-cdf.csv" "$BASE_DIR/tail-completion-cdf.csv"
cp "$SOURCE_DIR/tail-vo-delay-cdf.csv" "$BASE_DIR/tail-vo-delay-cdf.csv"
cp "$SOURCE_DIR/tail-vo-jitter-cdf.csv" "$BASE_DIR/tail-vo-jitter-cdf.csv"
cp "$SOURCE_DIR/tail-vo-completion-cdf.csv" "$BASE_DIR/tail-vo-completion-cdf.csv"

if [[ -f "$SOURCE_DIR/tail-cdf.plt" ]]; then
    cp "$SOURCE_DIR/tail-cdf.plt" "$BASE_DIR/tail-cdf.plt"
fi

echo "Baseline summary saved to $SUMMARY_TXT"
ELAPSED_TIME=$(($SECONDS - $START_TIME))
echo "Total execution time: $(($ELAPSED_TIME / 60)) minutes and $(($ELAPSED_TIME % 60)) seconds."
