"""Test FW-5 snapshot lifecycle in isolation."""
import urllib.request
import urllib.error
import json
import time

BASE = "http://balance.local"

def api(method, path, body=None):
    data = json.dumps(body).encode() if body else None
    req = urllib.request.Request(f"{BASE}{path}", data=data, method=method,
                                headers={"Content-Type": "application/json"} if data else {})
    try:
        resp = urllib.request.urlopen(req, timeout=10)
        try:
            return resp.status, json.loads(resp.read().decode())
        except json.JSONDecodeError:
            return resp.status, {}
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read().decode())
        except json.JSONDecodeError:
            return e.code, {}

print("=== FW-5 Snapshot Lifecycle Test ===\n")

# Step 1: There's currently a valid snapshot from the previous test run.
# Start a NEW test then immediately stop (abort) - this increments currentRunId
# but does NOT freeze a new snapshot. Existing snapshot becomes stale.
print("Step 1: Start new test + immediate abort (makes old snapshot stale)")
s, b = api("POST", "/cmd/start_test", {"profileId": "1750"})
print(f"  start_test: {s} {json.dumps(b)}")
time.sleep(2)
s, b = api("POST", "/cmd/stop")
print(f"  stop (abort): {s} {json.dumps(b)}")
time.sleep(1)

# Step 2: Now try save_session - should be 409 stale_result (old snapshot's runId < currentRunId)
print("\nStep 2: Try save_session (expect 409 stale_result)")
s, b = api("POST", "/cmd/save_session", {"name": "should_fail", "notes": ""})
print(f"  save_session: {s} {json.dumps(b)}")
if s == 409 and b.get("err") == "stale_result":
    print("  >>> PASS: stale save correctly rejected!")
    print(f"  resultRunId={b.get('resultRunId')}, currentRunId={b.get('currentRunId')}")
elif s == 409 and b.get("err") == "no_results":
    print("  >>> PASS: no results available (no snapshot was frozen)")
else:
    print(f"  >>> FAIL: expected 409 stale/no_results, got {s}")

# Step 3: Run a complete test to get fresh results
print("\nStep 3: Run complete test (1750 RPM profile, wait for RESULTS)")
s, b = api("POST", "/cmd/start_test", {"profileId": "1750"})
print(f"  start_test: {s}")

# Poll telemetry via WebSocket is ideal, but we'll just wait
# The 1750 profile has 3s spinup + 10s measure window = ~15s
print("  Waiting 25s for test to complete...")
time.sleep(25)

# Step 4: Save should succeed - fresh snapshot from this run
print("\nStep 4: Save session (expect 200 ok)")
s, b = api("POST", "/cmd/save_session", {"name": "valid_test", "notes": "lifecycle test"})
print(f"  save_session: {s} {json.dumps(b)}")
if s == 200 and b.get("ok"):
    print(f"  >>> PASS: saved with id={b.get('id')}")
else:
    print(f"  >>> FAIL: expected 200 ok")

# Step 5: Stop motor (should be no-op since RESULTS already stopped it)
s, b = api("POST", "/cmd/stop")
print(f"\nStep 5: Stop: {s}")

# Step 6: Save again after stop - should still work (same run)
time.sleep(1)
print("\nStep 6: Save again after stop (same run, expect 200)")
s, b = api("POST", "/cmd/save_session", {"name": "post_stop_save", "notes": "after stop"})
print(f"  save_session: {s} {json.dumps(b)}")
if s == 200 and b.get("ok"):
    print(f"  >>> PASS: post-stop save succeeded with id={b.get('id')}")
else:
    print(f"  >>> FAIL: expected 200 ok after stop")

# Step 7: Start new test + abort, then try stale save again
print("\nStep 7: Start new test + abort (invalidate snapshot)")
s, b = api("POST", "/cmd/start_test", {"profileId": "1750"})
print(f"  start_test: {s}")
time.sleep(2)
s, b = api("POST", "/cmd/stop")
print(f"  stop: {s}")
time.sleep(1)

print("\nStep 8: Save (expect 409 stale)")
s, b = api("POST", "/cmd/save_session", {"name": "stale2", "notes": ""})
print(f"  save_session: {s} {json.dumps(b)}")
if s == 409:
    print(f"  >>> PASS: stale save correctly rejected! err={b.get('err')}")
else:
    print(f"  >>> FAIL: expected 409, got {s}")

print("\n=== Done ===")
