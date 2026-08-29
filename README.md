# Context-Aware Adaptive Runtime Monitoring — ESP32 Research Experiment

Experimental evaluation of **static** vs. **context-aware adaptive** runtime monitoring on a dual-core ESP32 running FreeRTOS. The project measures whether a monitor that adapts its own intensity to computational pressure can reduce monitoring cost without sacrificing critical observability.

## Hardware & Software Setup

| Component | Configuration |
|---|---|
| Platform | Dual-core ESP32 |
| RTOS | FreeRTOS |
| Monitored task core | Core 0 |
| Runtime monitor core | Core 1 (runs asynchronously, isolated from the monitored task) |
| Task period | 100 ms |
| Task deadline | 50 ms (strict) |

Running the monitor on Core 1 keeps its execution isolated from the monitored task and avoids starving the FreeRTOS idle task under normal conditions — idle-task starvation was later found to be a hard stability boundary under extreme contention (see **Challenges** below).

## Repository / Experiment Structure

The experiment set is organized into three parts, each run across the same three workload conditions:

```
Part 1 — Baseline (no monitor)
Part 2 — Static Runtime Monitor
Part 3 — Context-Aware Adaptive Runtime Monitor

Each part → 3 experiments:
  E1: Normal workload
  E2: Heavy workload
  E3: Core Contention workload
```

| Exp. ID | Part | Workload | Iterations | Notes |
|---|---|---|---|---|
| P1-E1 | Baseline | Normal | 3,500 | No monitor attached |
| P1-E2 | Baseline | Heavy | 10,000 | No monitor attached |
| P1-E3 | Baseline | Contention | 4,500 (+ 5,000-iteration contender) | 10 ms contender delay |
| P2-E1 | Static | Normal | 3,500 | Continuous, fixed-cost monitoring |
| P2-E2 | Static | Heavy | 10,000 | Continuous, fixed-cost monitoring |
| P2-E3 | Static | Contention | 4,500 (+ contender) | Continuous, fixed-cost monitoring |
| P3-E1 | Adaptive | Normal | 3,500 | Mode-switching monitor |
| P3-E2 | Adaptive | Heavy | 10,000 | Mode-switching monitor |
| P3-E3 | Adaptive | Contention | 4,500 (+ contender) | Mode-switching monitor |

An additional **stress test** (10,000 iterations, aggressive load-shedding) was run against the adaptive monitor only, to probe the lower bound of monitoring coverage.

## Workload / Boundary Conditions

- **Normal workload**: 3,500 iterations, comfortably below the 50 ms deadline. Static and predictable by construction.
- **Heavy workload**: 10,000 iterations, consistently exceeds the deadline. Also static and predictable — just heavier than Normal.
- **Core contention workload**: 4,500 monitored-task iterations running against a 5,000-iteration contender task (10 ms delay) sharing Core 0. This is the only condition with genuine, unpredictable execution-time variance.

### Adaptive Monitor Configuration

- **Adaptation interval**: 10-sample decimation (mode transitions are evaluated once every 10 cycles, not continuously)
- **Hysteresis**: 3 cycles of sustained evidence required before a mode switch is allowed
- **Mode thresholds** (relative to the 50 ms deadline):
  - `FULL`: < 25 ms
  - `BALANCED`: < 40 ms
  - `LIGHT`: ≤ 50 ms
  - `CRITICAL`: above deadline

## Metrics Collected

Each experiment records:
- Average execution time & standard deviation
- Deadline misses (count and %)
- Monitored-task CPU utilization
- Runtime-monitor CPU utilization
- Monitoring overhead (absolute, ms) and overhead (%)
- Monitoring coverage (%) and events processed
- Mode distribution (adaptive only): sample counts per FULL/BALANCED/LIGHT/CRITICAL (or MINIMAL under stress)
- Free heap, minimum free heap, stack high-water mark

## Key Results

| Condition | Metric | Static | Adaptive | Result |
|---|---|---|---|---|
| Normal | Monitor CPU | 0.004% | 0.002% | Adaptive -50% |
| Heavy | Monitor CPU | 0.003% | 0.006% | Adaptive +100% |
| Contention | Monitor CPU | 0.003% | 0.002% | Adaptive −33.3% |
| Contention | Coverage | 100% | 100% | Same |
| Stress test | Coverage | — | 11.1% | Aggressive load-shedding limit |

**The adaptive monitor only outperformed the static monitor under normal workload & core contention** — the one condition with genuine,
unpredictable fluctuation. Under Heavy workloads, the adaptive monitor's own control logic (hysteresis + decimation evaluation) cost more 
than it saved, because there was no fluctuation for it to react to. This is not a design failure — the adaptive controller is built 
specifically to sense and respond to change, and two of the three test conditions contained none by design.

## Challenges Encountered

1. **Transition-overhead trap** — the first ("naive") adaptive implementation checked state every cycle; the CPU cycles saved by skipping calculations were fully offset by branching/pipeline-stall overhead of the decision logic itself.
2. **Mode thrashing** — execution times near a threshold boundary caused rapid, repeated mode switching before hysteresis was introduced.
3. **Aggressive load-shedding blindness** — pushed far enough, the adaptive monitor's coverage collapsed to 11.1% in the stress test, sampling only 222 of a possible 2,000 events.
4. **Hard stability boundary** — under extreme contention, competition for CPU cycles prevented the FreeRTOS idle task from running, triggering the ESP32 hardware watchdog and a full system reset.
5. **Memory measurement ambiguity** — endpoint free-heap snapshots were inconsistent (adaptive reported *lower* free heap than static under contention) and are not sufficient on their own to prove or disprove queue-specific memory savings.

## Conclusion

The experiments show that context-aware monitoring is not universally superior to static monitoring. Under sustained
heavy workload, static monitoring achieved lower runtime-monitor CPU utilization. Under normal workload, the contextaware monitor achieved a
50% reduction in monitor CPU utilization, and under core contention it achieved
approximately a 33% reduction, while maintaining corrected 100% coverage 
The main conclusion is that adaptation is most meaningful when computational context changes. Stable light and
stable heavy synthetic workloads do not fully represent the fluctuating workloads for which context-aware monitoring
is intended. Future work should therefore evaluate bursty, dynamic and recovery-based workloads. The negative and
mixed findings are themselves valuable: they identify where adaptive control costs more than it saves and where it
begins to provide measurable benefit.

## Future Work

- Evaluate the adaptive monitor against genuinely real-world fluctuating workloads (sensor bursts, network jitter, mixed task loads), not only synthetic core contention
- Enforce a minimum coverage floor to prevent collapse under aggressive load-shedding
- Measure queue high-water occupancy directly instead of inferring from free-heap snapshots
- Separate transition-control cost from detailed monitoring-calculation cost in measurement
- Repeat experiments across multiple independent runs and report confidence intervals
- Test multiple contention intensities and contender duty cycles

## Reference

Full methodology, per-experiment tables, and visual comparisons are in `Adaptive_Runtime_Monitor_Research_Report.pdf`.
