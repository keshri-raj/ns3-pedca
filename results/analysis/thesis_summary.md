# Thesis Analysis Summary

Generated from paired Dist0/Dist1 tail-summary.csv files under [results](C:/ns-3/results).

## Visuals

![Throughput deltas](C:/ns-3/results/analysis/throughput_deltas.svg)

![Fairness and delay deltas](C:/ns-3/results/analysis/fairness_delay_deltas.svg)

![P-EDCA efficiency deltas](C:/ns-3/results/analysis/pedca_efficiency_deltas.svg)

![Delay tails by case](C:/ns-3/results/analysis/delay_tails_by_case.svg)

![Throughput shift by case](C:/ns-3/results/analysis/throughput_shift_by_case.svg)

![Absolute VO delay tails by case](C:/ns-3/results/analysis/delay_tails_absolute_by_case.png)

![VO delay p95 by case](C:/ns-3/results/analysis/vo_delay_p95_by_case.png)

![VO delay p99 by case](C:/ns-3/results/analysis/vo_delay_p99_by_case.png)

## Thesis-Style Summary

Restricting P-EDCA to a single balanced link (Dist1) has a clear effect on the system, but the effect is workload-dependent rather than uniformly beneficial or harmful. Across the analyzed result pairs, one-link P-EDCA improved P-EDCA procedural efficiency in most runs by reducing P-EDCA failures in 5 cases and P-EDCA resets in 7 cases. This is the most stable cross-experiment observation.

At the application level, the VO-throughput effect is mixed. One-link P-EDCA increased average P-EDCA VO throughput per STA in 4 cases, but reduced it in 2 cases. The strongest gains appear in low-to-moderate penetration cases such as 2:18, 3:7, 5:5, and 3:27 (40 MHz, VO=4). In contrast, higher-share cases such as 9:21 and some 15:15 configurations often show both-links P-EDCA providing the larger raw P-EDCA VO advantage.

Fairness also changes in both directions. JFI improved meaningfully in 1 runs and worsened meaningfully in 3 runs. Delay tails, however, often benefited from one-link P-EDCA: VO delay p95 improved by more than 10 ms in 4 runs. Aggregate throughput increased noticeably in 1 runs and decreased noticeably in 4 runs, reinforcing that the one-link policy is best understood as a tradeoff between P-EDCA aggressiveness and contention control rather than a universal improvement.

## Selected Thesis Cases

- pedca_15_edca_15_sim_10s_selected: aggregate delta 2.20 Mbit/s, P-EDCA VO/STA delta -0.016 Mbit/s, EDCA VO/STA delta 0.158 Mbit/s, JFI delta -0.091.
- pedca_9_edca_21_sim_10s_selected: aggregate delta -0.85 Mbit/s, P-EDCA VO/STA delta -0.335 Mbit/s, EDCA VO/STA delta 0.103 Mbit/s, JFI delta 0.003.

## Files

- [analysis_summary.csv](C:/ns-3/results/analysis/analysis_summary.csv)
- [throughput_deltas.svg](C:/ns-3/results/analysis/throughput_deltas.svg)
- [fairness_delay_deltas.svg](C:/ns-3/results/analysis/fairness_delay_deltas.svg)
- [pedca_efficiency_deltas.svg](C:/ns-3/results/analysis/pedca_efficiency_deltas.svg)
- [delay_tails_by_case.svg](C:/ns-3/results/analysis/delay_tails_by_case.svg)
- [delay_tails_by_case.png](C:/ns-3/results/analysis/delay_tails_by_case.png)
- [throughput_shift_by_case.svg](C:/ns-3/results/analysis/throughput_shift_by_case.svg)
- [throughput_shift_by_case.png](C:/ns-3/results/analysis/throughput_shift_by_case.png)
- [delay_tails_absolute_by_case.png](C:/ns-3/results/analysis/delay_tails_absolute_by_case.png)
- [vo_delay_p95_by_case.png](C:/ns-3/results/analysis/vo_delay_p95_by_case.png)
- [vo_delay_p99_by_case.png](C:/ns-3/results/analysis/vo_delay_p99_by_case.png)
- [vo_delay_p95_by_case_clean.png](C:/ns-3/results/analysis/vo_delay_p95_by_case_clean.png)
- [vo_delay_p99_by_case_clean.png](C:/ns-3/results/analysis/vo_delay_p99_by_case_clean.png)
- [vo_delay_p95_grouped_bar.png](C:/ns-3/results/analysis/vo_delay_p95_grouped_bar.png)
