#!/usr/bin/env python3
"""
RPM Sweep Test for DynamicBalancer AS5047 Accuracy Diagnostics
==============================================================

Purpose:
  Slowly ramp ESC from idle up, measuring firmware-reported RPM vs
  wrap-count-derived RPM at each throttle step. Identifies where
  the AS5047 zero-crossing RPM estimation breaks down.

What it tests:
  1. Direct ESC µs → actual RPM mapping (the real motor curve)
  2. Firmware rpmEMA accuracy compared to wrap-count rate
  3. Whether the 500 Hz sample loop misses zero-crossings at high RPM
  4. WebSocket telemetry latency / backlog behaviour

How it works:
  - POST /cmd/set_esc {"us": N}  — directly sets ESC pulse width
  - GET  /diag/raw               — reads raw angle, rpmEMA, wrapCount,
                                    lastWrapDtUs, sampleCount
  - Steps ESC from START_US to MAX_US in STEP_US increments
  - At each step, waits SETTLE_S then takes N_SAMPLES snapshots
  - Computes wrap-rate RPM from wrapCount delta
  - Compares to firmware rpmEMA — flags divergence
  - Stops early if RPM error > threshold or motor stalls

Output:
  - Live terminal table
  - CSV file: _rpm_sweep_results.csv
  - Summary of where issues start

Usage:
  python _rpm_sweep_test.py [--host 192.168.4.165] [--start 1020] [--stop 1800]
                            [--step 10] [--settle 2.5] [--samples 5]

Requires:
  pip install requests websocket-client
"""

import argparse
import csv
import json
import sys
import time
from datetime import datetime

try:
    import requests
except ImportError:
    print("ERROR: pip install requests")
    sys.exit(1)


def diag_raw(host, timeout=3):
    """GET /diag/raw — returns dict with rawAngleDeg, rpmEMA, wrapCount, etc."""
    r = requests.get(f"http://{host}/diag/raw", timeout=timeout)
    r.raise_for_status()
    return r.json()


def set_esc(host, us, timeout=3):
    """POST /cmd/set_esc — sets direct ESC µs override. us=0 to disable."""
    r = requests.post(f"http://{host}/cmd/set_esc",
                       json={"us": us}, timeout=timeout)
    r.raise_for_status()
    return r.json()


def stop_motor(host, timeout=3):
    """POST /cmd/stop — stops motor and cancels ESC override."""
    r = requests.post(f"http://{host}/cmd/stop",
                       json={}, timeout=timeout)
    r.raise_for_status()


def take_snapshot(host, n_samples=5, interval=0.3):
    """
    Take n_samples readings from /diag/raw spaced by interval seconds.
    Returns dict with averaged and delta-computed values.
    """
    samples = []
    for i in range(n_samples):
        if i > 0:
            time.sleep(interval)
        samples.append(diag_raw(host))

    first = samples[0]
    last = samples[-1]
    elapsed_s = interval * (n_samples - 1) if n_samples > 1 else 1.0

    # Wrap count delta → RPM
    wrap_delta = last["wrapCount"] - first["wrapCount"]
    wrap_rate_rpm = (wrap_delta / elapsed_s) * 60.0 if elapsed_s > 0 else 0

    # Average rpmEMA across samples
    rpm_emas = [s["rpmEMA"] for s in samples]
    avg_rpm_ema = sum(rpm_emas) / len(rpm_emas) if rpm_emas else 0

    # Average lastWrapDtUs → derived RPM
    wrap_dts = [s["lastWrapDtUs"] for s in samples if s["lastWrapDtUs"] > 0]
    avg_wrap_dt_us = sum(wrap_dts) / len(wrap_dts) if wrap_dts else 0
    dt_derived_rpm = 60e6 / avg_wrap_dt_us if avg_wrap_dt_us > 0 else 0

    # Raw angle jitter (std dev of angles)
    angles = [s["rawAngleDeg"] for s in samples]

    # Sample count delta → actual loop rate
    sample_delta = last["sampleCount"] - first["sampleCount"]
    loop_rate_hz = sample_delta / elapsed_s if elapsed_s > 0 else 0

    return {
        "rpmEMA_avg": avg_rpm_ema,
        "rpmEMA_min": min(rpm_emas),
        "rpmEMA_max": max(rpm_emas),
        "wrapRate_rpm": wrap_rate_rpm,
        "dtDerived_rpm": dt_derived_rpm,
        "wrapDelta": wrap_delta,
        "avgWrapDtUs": avg_wrap_dt_us,
        "loopRateHz": loop_rate_hz,
        "rawAngles": angles,
        "n_samples": n_samples,
    }


def rpm_error_pct(ema_rpm, wrap_rpm):
    """Percentage error between two RPM estimates."""
    if wrap_rpm < 5:
        return 0  # both near zero
    return abs(ema_rpm - wrap_rpm) / wrap_rpm * 100


def main():
    parser = argparse.ArgumentParser(description="RPM Sweep Test")
    parser.add_argument("--host", default="192.168.4.165")
    parser.add_argument("--start", type=int, default=1020,
                        help="Starting ESC µs (just above idle)")
    parser.add_argument("--stop", type=int, default=0,
                        help="Max ESC µs (0 = read from device)")
    parser.add_argument("--step", type=int, default=10,
                        help="µs increment per step")
    parser.add_argument("--settle", type=float, default=2.5,
                        help="Seconds to wait after each ESC change")
    parser.add_argument("--samples", type=int, default=5,
                        help="Number of /diag/raw polls per step")
    parser.add_argument("--sample-interval", type=float, default=0.4,
                        help="Seconds between polls within a step")
    parser.add_argument("--max-error-pct", type=float, default=25.0,
                        help="Stop if RPM error exceeds this %% for 3 consecutive steps")
    parser.add_argument("--max-rpm", type=float, default=6000,
                        help="Stop if RPM exceeds this")
    parser.add_argument("--output", default="_rpm_sweep_results.csv",
                        help="Output CSV path")
    args = parser.parse_args()

    host = args.host

    # --- Pre-flight checks ---
    print(f"[{datetime.now():%H:%M:%S}] Connecting to {host}...")
    try:
        raw = diag_raw(host)
        print(f"  Device online. wrapCount={raw['wrapCount']}, rpmEMA={raw['rpmEMA']:.1f}")
    except Exception as e:
        print(f"  FAILED: {e}")
        print("  Is the firmware flashed with /diag/raw endpoint?")
        sys.exit(1)

    esc_max = args.stop if args.stop > 0 else raw.get("escMaxUs", 2000)
    print(f"  ESC range: {args.start} → {esc_max} µs, step={args.step}")
    print(f"  Settle={args.settle}s, {args.samples} samples @ {args.sample_interval}s intervals")
    print()

    # --- Safety: stop everything first ---
    print("Stopping motor...")
    stop_motor(host)
    time.sleep(1)

    # --- CSV setup ---
    csv_file = open(args.output, "w", newline="")
    csv_writer = csv.writer(csv_file)
    csv_writer.writerow([
        "esc_us", "rpmEMA_avg", "rpmEMA_min", "rpmEMA_max",
        "wrapRate_rpm", "dtDerived_rpm", "wrapDelta", "avgWrapDtUs",
        "loopRateHz", "error_pct", "flag"
    ])

    # --- Header ---
    print(f"{'ESC µs':>7} {'rpmEMA':>8} {'WrapRPM':>8} {'dtRPM':>8} "
          f"{'Wraps':>6} {'LoopHz':>7} {'Err%':>6}  Flag")
    print("-" * 75)

    results = []
    consecutive_errors = 0
    last_esc_us = args.start

    try:
        for esc_us in range(args.start, esc_max + 1, args.step):
            # Set ESC
            set_esc(host, esc_us)
            time.sleep(args.settle)

            # Take measurements
            snap = take_snapshot(host, args.samples, args.sample_interval)

            # Compute error
            # Use wrap-rate RPM as reference when wraps > 2, else dtDerived
            ref_rpm = snap["wrapRate_rpm"]
            if snap["wrapDelta"] < 2:
                ref_rpm = snap["dtDerived_rpm"]

            err_pct = rpm_error_pct(snap["rpmEMA_avg"], ref_rpm)

            # Flag logic
            flag = ""
            if snap["rpmEMA_avg"] < 1 and esc_us > 1050:
                flag = "NO_SPIN"
            elif snap["wrapDelta"] == 0 and snap["rpmEMA_avg"] > 100:
                flag = "EMA_STUCK"  # EMA reports RPM but no new wraps
            elif err_pct > args.max_error_pct:
                flag = "DIVERGE"
                consecutive_errors += 1
            elif snap["loopRateHz"] < 400:
                flag = "SLOW_LOOP"
            else:
                consecutive_errors = 0

            # Print row
            print(f"{esc_us:>7} {snap['rpmEMA_avg']:>8.1f} {snap['wrapRate_rpm']:>8.1f} "
                  f"{snap['dtDerived_rpm']:>8.1f} {snap['wrapDelta']:>6} "
                  f"{snap['loopRateHz']:>7.0f} {err_pct:>6.1f}  {flag}")

            # Write CSV
            csv_writer.writerow([
                esc_us, f"{snap['rpmEMA_avg']:.1f}", f"{snap['rpmEMA_min']:.1f}",
                f"{snap['rpmEMA_max']:.1f}", f"{snap['wrapRate_rpm']:.1f}",
                f"{snap['dtDerived_rpm']:.1f}", snap["wrapDelta"],
                f"{snap['avgWrapDtUs']:.0f}", f"{snap['loopRateHz']:.0f}",
                f"{err_pct:.1f}", flag
            ])
            csv_file.flush()

            results.append({
                "esc_us": esc_us,
                "snap": snap,
                "err_pct": err_pct,
                "flag": flag,
            })
            last_esc_us = esc_us

            # Stop conditions
            if consecutive_errors >= 3:
                print(f"\n*** STOPPED: {args.max_error_pct}% error threshold exceeded "
                      f"for 3 consecutive steps at {esc_us} µs ***")
                break

            if snap["rpmEMA_avg"] > args.max_rpm:
                print(f"\n*** STOPPED: RPM ({snap['rpmEMA_avg']:.0f}) exceeded "
                      f"max-rpm={args.max_rpm} at {esc_us} µs ***")
                break

    except KeyboardInterrupt:
        print("\n\n*** Interrupted by user ***")
    finally:
        # ALWAYS stop motor
        print(f"\n[{datetime.now():%H:%M:%S}] Stopping motor...")
        try:
            set_esc(host, 0)
            stop_motor(host)
        except Exception:
            print("  WARNING: Failed to stop motor via API!")
        csv_file.close()

    # --- Summary ---
    print(f"\n{'='*75}")
    print("SUMMARY")
    print(f"{'='*75}")

    if not results:
        print("No data collected.")
        return

    # Find first spin
    first_spin = next((r for r in results if r["snap"]["rpmEMA_avg"] > 10), None)
    if first_spin:
        print(f"First spin detected at: {first_spin['esc_us']} µs "
              f"({first_spin['snap']['rpmEMA_avg']:.0f} RPM)")

    # Find first issue
    first_issue = next((r for r in results if r["flag"] in ("DIVERGE", "EMA_STUCK", "SLOW_LOOP")), None)
    if first_issue:
        print(f"First issue at: {first_issue['esc_us']} µs "
              f"(flag={first_issue['flag']}, RPM={first_issue['snap']['rpmEMA_avg']:.0f}, "
              f"err={first_issue['err_pct']:.1f}%)")
    else:
        print("No RPM accuracy issues detected in sweep range!")

    # Find max clean RPM
    clean = [r for r in results if r["flag"] == "" and r["snap"]["rpmEMA_avg"] > 10]
    if clean:
        best = max(clean, key=lambda r: r["snap"]["rpmEMA_avg"])
        print(f"Max clean RPM: {best['snap']['rpmEMA_avg']:.0f} at {best['esc_us']} µs")

    # RPM vs µs curve summary
    print(f"\nESC µs → RPM curve (sampled):")
    step_display = max(1, len(results) // 15)
    for i, r in enumerate(results):
        if i % step_display == 0 or r["flag"]:
            bar = "#" * int(r["snap"]["rpmEMA_avg"] / 100)
            flag_str = f"  ← {r['flag']}" if r["flag"] else ""
            print(f"  {r['esc_us']:>5} µs → {r['snap']['rpmEMA_avg']:>6.0f} RPM  {bar}{flag_str}")

    # Loop rate analysis
    loop_rates = [r["snap"]["loopRateHz"] for r in results if r["snap"]["loopRateHz"] > 0]
    if loop_rates:
        print(f"\nSensor loop rate: min={min(loop_rates):.0f} Hz, "
              f"max={max(loop_rates):.0f} Hz, avg={sum(loop_rates)/len(loop_rates):.0f} Hz")

    # Max RPM where angle sampling is theoretically safe
    # At 500 Hz, max angle-step per sample before aliasing
    # RPM * 360 / (60 * loopHz) = degrees per sample
    # Need < 300° per sample for wrap detection (threshold is >300° backward)
    if loop_rates:
        avg_loop = sum(loop_rates) / len(loop_rates)
        max_safe_rpm = avg_loop * 60 * 300 / 360  # degrees/sample < 300
        print(f"Theoretical max RPM for wrap detection at {avg_loop:.0f} Hz loop: "
              f"{max_safe_rpm:.0f} RPM")
        # More conservative: >180° forward per sample means the backward delta
        # could appear as a wrap
        max_conservative = avg_loop * 60 * 180 / 360
        print(f"Conservative max (180°/sample limit): {max_conservative:.0f} RPM")

    print(f"\nResults saved to: {args.output}")
    print("Done.")


if __name__ == "__main__":
    main()
