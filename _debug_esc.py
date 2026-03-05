#!/usr/bin/env python3
"""Quick debug script for ESC override and raw angle readings."""
import requests, time, json

HOST = "192.168.4.165"

def raw():
    return requests.get(f"http://{HOST}/diag/raw", timeout=3).json()

def show(r):
    print(f"  angle={r['rawAngleDeg']:.2f}  rpm={r['rpmEMA']:.1f}  "
          f"wraps={r['wrapCount']}  samples={r['sampleCount']}  sweep={r['sweepEscUs']}")

# Check baseline
print("=== BASELINE (motor off) ===")
for i in range(3):
    show(raw())
    time.sleep(0.3)

# Try setting ESC to 1200
print("\n=== SET ESC=1200 ===")
resp = requests.post(f"http://{HOST}/cmd/set_esc", json={"us": 1200}, timeout=3).json()
print(f"  API response: {resp}")

time.sleep(4)
print("After 4s settle:")
for i in range(5):
    show(raw())
    time.sleep(0.5)

# Try higher: 1500
print("\n=== SET ESC=1500 ===")
resp = requests.post(f"http://{HOST}/cmd/set_esc", json={"us": 1500}, timeout=3).json()
print(f"  API response: {resp}")

time.sleep(4)
print("After 4s settle:")
for i in range(5):
    show(raw())
    time.sleep(0.5)

# Try even higher: 1800
print("\n=== SET ESC=1800 ===")
resp = requests.post(f"http://{HOST}/cmd/set_esc", json={"us": 1800}, timeout=3).json()
print(f"  API response: {resp}")

time.sleep(4)
print("After 4s settle:")
for i in range(5):
    show(raw())
    time.sleep(0.5)

# Now try a normal test to see if THAT works
print("\n=== STOP ESC OVERRIDE ===")
requests.post(f"http://{HOST}/cmd/set_esc", json={"us": 0}, timeout=3)
requests.post(f"http://{HOST}/cmd/stop", json={}, timeout=3)
time.sleep(2)

print("\n=== START NORMAL TEST at 2500 RPM ===")
requests.post(f"http://{HOST}/cmd/start_test",
              json={"profileId": "2500"}, timeout=3)
time.sleep(6)
print("After 6s:")
for i in range(5):
    show(raw())
    time.sleep(0.5)

# Stop
requests.post(f"http://{HOST}/cmd/stop", json={}, timeout=3)
print("\nStopped. Done.")
