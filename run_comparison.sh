#!/bin/bash
START_TIME=$SECONDS
export PATH=/c/ns-3/build/lib:/c/msys64/mingw64/bin:$PATH
EXE=./build/scratch/ns3-dev-uhr-mld-mixed-pedca-default.exe

# Configuration: Accept arguments from command line or use defaults
PEDCA=${1:-5}
EDCA=${2:-5}
TOTAL_STAS=$((PEDCA + EDCA))
SIMTIME=10.0
SIMTIME_TAG=$(printf "%s" "$SIMTIME" | sed 's/\.0$//; s/\./p/g')
BASE_DIR="results/pedca_${PEDCA}_edca_${EDCA}_sim_${SIMTIME_TAG}s"
ENABLE_UL=true
ENABLE_DL=false
VO_ONLY_TRAFFIC=false
MAX_BYTES_PER_FLOW=0
APP_START=5.0
APP_START_SPREAD=2.0
MEAN_PROFILE_MS=250
PROBABILITY_TWO_FLOWS=0.15
EHT_MCS=5
CHANNEL_WIDTH=40
PEDCA_SELECTION_SEED=7

# P-EDCA acts on AC_VO, so this stress configuration keeps mixed AC traffic
# but makes VO dominant and uses a slightly less forgiving PHY so both-links
# vs one-link effects have a better chance to show up in throughput/fairness.
case "$TOTAL_STAS" in
    10)
        BK_RATE_MBPS=0.1
        BE_RATE_MBPS=0.25
        VI_RATE_MBPS=0.5
        VO_RATE_MBPS=7
        ;;
    20)
        BK_RATE_MBPS=0.02
        BE_RATE_MBPS=0.05
        VI_RATE_MBPS=0.15
        VO_RATE_MBPS=4.5
        ;;
    30)
        BK_RATE_MBPS=0.02
        BE_RATE_MBPS=0.05
        VI_RATE_MBPS=0.1
        VO_RATE_MBPS=4
        ;;
    40)
        BK_RATE_MBPS=0.02
        BE_RATE_MBPS=0.04
        VI_RATE_MBPS=0.08
        VO_RATE_MBPS=2.0
        ;;
    50)
        BK_RATE_MBPS=0.01
        BE_RATE_MBPS=0.03
        VI_RATE_MBPS=0.06
        VO_RATE_MBPS=1.6
        ;;
    *)
        echo "Unsupported total STA count: $TOTAL_STAS"
        echo "This script currently auto-tunes traffic rates for 10, 20, 30, 40, or 50 total clients."
        echo "Please call it with PEDCA+EDCA equal to 10, 20, 30, 40, or 50."
        exit 1
        ;;
esac

echo "=========================================================="
echo " Starting Fairness Experiment"
echo " Configuration: $PEDCA P-EDCA STAs, $EDCA EDCA STAs, $SIMTIME sec"
echo " Total STAs: $TOTAL_STAS"
echo " Workload: UDP, UL=$ENABLE_UL, DL=$ENABLE_DL"
echo " Traffic profile: VO_ONLY=$VO_ONLY_TRAFFIC, MAX_BYTES_PER_FLOW=$MAX_BYTES_PER_FLOW"
echo " Timing/profile: APP_START=$APP_START, APP_START_SPREAD=$APP_START_SPREAD, MEAN_PROFILE_MS=$MEAN_PROFILE_MS, TWO_FLOWS=$PROBABILITY_TWO_FLOWS"
echo " PHY stress settings: EHT_MCS=$EHT_MCS, CHANNEL_WIDTH=${CHANNEL_WIDTH}MHz"
echo " P-EDCA selection seed: $PEDCA_SELECTION_SEED"
echo " Offered load per AC (Mbps): BK=$BK_RATE_MBPS, BE=$BE_RATE_MBPS, VI=$VI_RATE_MBPS, VO=$VO_RATE_MBPS"
echo "=========================================================="
echo ""

echo "----------------------------------------------------------"
echo " Running Scenario 1: P-EDCA uses both links"
echo "----------------------------------------------------------"
"$EXE" --numPedcaStas=$PEDCA --numEdcaStas=$EDCA --simTime=$SIMTIME --distributePedcaAcrossLinks=false --enableAnim=false --enableUl=$ENABLE_UL --enableDl=$ENABLE_DL --voOnlyTraffic=$VO_ONLY_TRAFFIC --maxBytesPerFlow=$MAX_BYTES_PER_FLOW --appStart=$APP_START --appStartSpread=$APP_START_SPREAD --meanProfileMs=$MEAN_PROFILE_MS --probabilityTwoFlows=$PROBABILITY_TWO_FLOWS --pedcaSelectionSeed=$PEDCA_SELECTION_SEED --ehtMcs=$EHT_MCS --channelWidth=$CHANNEL_WIDTH --bkRateMbps=$BK_RATE_MBPS --beRateMbps=$BE_RATE_MBPS --viRateMbps=$VI_RATE_MBPS --voRateMbps=$VO_RATE_MBPS

echo ""
echo "----------------------------------------------------------"
echo " Running Scenario 2: P-EDCA restricted to 1 link (Fair)"
echo "----------------------------------------------------------"
"$EXE" --numPedcaStas=$PEDCA --numEdcaStas=$EDCA --simTime=$SIMTIME --distributePedcaAcrossLinks=true --enableAnim=false --enableUl=$ENABLE_UL --enableDl=$ENABLE_DL --voOnlyTraffic=$VO_ONLY_TRAFFIC --maxBytesPerFlow=$MAX_BYTES_PER_FLOW --appStart=$APP_START --appStartSpread=$APP_START_SPREAD --meanProfileMs=$MEAN_PROFILE_MS --probabilityTwoFlows=$PROBABILITY_TWO_FLOWS --pedcaSelectionSeed=$PEDCA_SELECTION_SEED --ehtMcs=$EHT_MCS --channelWidth=$CHANNEL_WIDTH --bkRateMbps=$BK_RATE_MBPS --beRateMbps=$BE_RATE_MBPS --viRateMbps=$VI_RATE_MBPS --voRateMbps=$VO_RATE_MBPS

echo ""
echo "=========================================================="
echo " Generating Gnuplot Comparison Scripts..."
echo "=========================================================="

mkdir -p results
mkdir -p "$BASE_DIR"

get_metric() {
    local file="$1"
    local key="$2"
    awk -F',' -v metric="$key" '$1 == metric { print $2; exit }' "$file"
}

write_comparison_summary() {
    local dist0_csv="$BASE_DIR/Dist0/tail-summary.csv"
    local dist1_csv="$BASE_DIR/Dist1/tail-summary.csv"
    local summary_txt="$BASE_DIR/compare_summary.txt"

    if [[ ! -f "$dist0_csv" || ! -f "$dist1_csv" ]]; then
        echo "Comparison summary skipped because one or both summary CSV files are missing."
        return
    fi

    cat > "$summary_txt" << EOF
Fairness Experiment Comparison Summary
=====================================
Configuration:
  P-EDCA STAs: $PEDCA
  EDCA STAs: $EDCA
  Total STAs: $TOTAL_STAS
  Simulation time: $SIMTIME sec
  Traffic mode: UDP uplink-only, mixed AC with VO-dominant stress
  PHY stress settings: EHT_MCS=$EHT_MCS CHANNEL_WIDTH=${CHANNEL_WIDTH}MHz
  P-EDCA selection seed: $PEDCA_SELECTION_SEED
  Offered load per AC (Mbps): BK=$BK_RATE_MBPS BE=$BE_RATE_MBPS VI=$VI_RATE_MBPS VO=$VO_RATE_MBPS
  App start / spread (s): $APP_START / $APP_START_SPREAD
  Dynamic profile: mean=${MEAN_PROFILE_MS}ms, two-flow probability=${PROBABILITY_TWO_FLOWS}
  Results folder: $BASE_DIR

Scenario 1: P-EDCA uses both links
----------------------------------
  Aggregate throughput (Mbit/s): $(get_metric "$dist0_csv" aggregate_throughput_mbps)
  P-EDCA throughput (Mbit/s):    $(get_metric "$dist0_csv" pedca_throughput_mbps)
  EDCA throughput (Mbit/s):      $(get_metric "$dist0_csv" edca_throughput_mbps)
  Jain's Fairness Index:         $(get_metric "$dist0_csv" jfi)
  Delay p95 (ms):                $(get_metric "$dist0_csv" delay_p95_ms)
  Jitter p95 (ms):               $(get_metric "$dist0_csv" jitter_p95_ms)
  VO delay p95 (ms):             $(get_metric "$dist0_csv" vo_delay_p95_ms)
  VO jitter p95 (ms):            $(get_metric "$dist0_csv" vo_jitter_p95_ms)
  P-EDCA selections:             $(get_metric "$dist0_csv" pedca_selections)
  P-EDCA CTS attempts:           $(get_metric "$dist0_csv" pedca_cts_attempts)
  P-EDCA successes:              $(get_metric "$dist0_csv" pedca_successes)
  P-EDCA failures:               $(get_metric "$dist0_csv" pedca_failures)
  P-EDCA resets:                 $(get_metric "$dist0_csv" pedca_resets)

Scenario 2: P-EDCA restricted to 1 link (Fair)
----------------------------------------------
  Aggregate throughput (Mbit/s): $(get_metric "$dist1_csv" aggregate_throughput_mbps)
  P-EDCA throughput (Mbit/s):    $(get_metric "$dist1_csv" pedca_throughput_mbps)
  EDCA throughput (Mbit/s):      $(get_metric "$dist1_csv" edca_throughput_mbps)
  Jain's Fairness Index:         $(get_metric "$dist1_csv" jfi)
  Delay p95 (ms):                $(get_metric "$dist1_csv" delay_p95_ms)
  Jitter p95 (ms):               $(get_metric "$dist1_csv" jitter_p95_ms)
  VO delay p95 (ms):             $(get_metric "$dist1_csv" vo_delay_p95_ms)
  VO jitter p95 (ms):            $(get_metric "$dist1_csv" vo_jitter_p95_ms)
  P-EDCA selections:             $(get_metric "$dist1_csv" pedca_selections)
  P-EDCA CTS attempts:           $(get_metric "$dist1_csv" pedca_cts_attempts)
  P-EDCA successes:              $(get_metric "$dist1_csv" pedca_successes)
  P-EDCA failures:               $(get_metric "$dist1_csv" pedca_failures)
  P-EDCA resets:                 $(get_metric "$dist1_csv" pedca_resets)

Delta (Scenario 2 - Scenario 1)
-------------------------------
  This summary is based on the two tail-summary.csv files generated by the simulation.
  Review the CSV and PNG outputs in $BASE_DIR for detailed analysis.
EOF

    echo "Comparison text summary saved to $summary_txt"
}

cat << EOF > "$BASE_DIR/compare_scenarios.plt"
set terminal pngcairo size 1280,720
set datafile separator ','
set grid
set key right bottom
set xrange [0:*]
set yrange [0:1]

# Delay CDF Plot
set output '${BASE_DIR}/compare_delay_cdf.png'
set title 'Fairness Comparison: Delay CDF (Scenario 1 vs Scenario 2)'
set xlabel 'Delay (ms)'
set ylabel 'CDF'

plot '${BASE_DIR}/Dist0/tail-delay-cdf.csv' using 1:2 with lines lw 3 title 'Scenario 1 (P-EDCA uses both links)', \\
     '${BASE_DIR}/Dist1/tail-delay-cdf.csv' using 1:2 with lines lw 3 title 'Scenario 2 (Fair: P-EDCA restricted to 1 link)'

# Jitter CDF Plot
set output '${BASE_DIR}/compare_jitter_cdf.png'
set title 'Fairness Comparison: Jitter CDF (Scenario 1 vs Scenario 2)'
set xlabel 'Jitter (ms)'

plot '${BASE_DIR}/Dist0/tail-jitter-cdf.csv' using 1:2 with lines lw 3 title 'Scenario 1 (P-EDCA uses both links)', \\
     '${BASE_DIR}/Dist1/tail-jitter-cdf.csv' using 1:2 with lines lw 3 title 'Scenario 2 (Fair: P-EDCA restricted to 1 link)'

# VO Delay CDF Plot
set output '${BASE_DIR}/compare_vo_delay_cdf.png'
set title 'Fairness Comparison: VO Delay CDF (Scenario 1 vs Scenario 2)'
set xlabel 'Delay (ms)'
set ylabel 'CDF'

plot '${BASE_DIR}/Dist0/tail-vo-delay-cdf.csv' using 1:2 with lines lw 3 title 'Scenario 1 VO', \\
     '${BASE_DIR}/Dist1/tail-vo-delay-cdf.csv' using 1:2 with lines lw 3 title 'Scenario 2 VO (Fair)'

# VO Jitter CDF Plot
set output '${BASE_DIR}/compare_vo_jitter_cdf.png'
set title 'Fairness Comparison: VO Jitter CDF (Scenario 1 vs Scenario 2)'
set xlabel 'Jitter (ms)'

plot '${BASE_DIR}/Dist0/tail-vo-jitter-cdf.csv' using 1:2 with lines lw 3 title 'Scenario 1 VO', \\
     '${BASE_DIR}/Dist1/tail-vo-jitter-cdf.csv' using 1:2 with lines lw 3 title 'Scenario 2 VO (Fair)'
EOF

write_comparison_summary

if command -v gnuplot &> /dev/null
then
    echo "Gnuplot detected. Automatically rendering PNGs..."
    gnuplot "$BASE_DIR/compare_scenarios.plt"
    echo "Graphs saved as overall and VO comparison PNGs in $BASE_DIR/"
else
    echo "Gnuplot is not installed."
    echo "Run: gnuplot $BASE_DIR/compare_scenarios.plt once installed."
fi

echo "All tasks finished successfully!"
ELAPSED_TIME=$(($SECONDS - $START_TIME))
echo "Total execution time: $(($ELAPSED_TIME / 60)) minutes and $(($ELAPSED_TIME % 60)) seconds."
