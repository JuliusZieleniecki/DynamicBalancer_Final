"""API validation tests for DynamicBalancer firmware."""
import urllib.request
import urllib.error
import json
import time
import sys

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
    except Exception as e:
        return -1, {"error": str(e)}

def test(name, status, body, expect_status, expect_keys=None):
    ok = status == expect_status
    detail = ""
    if expect_keys:
        for k, v in expect_keys.items():
            if body.get(k) != v:
                ok = False
                detail += f" {k}={body.get(k)} (expected {v})"
    sym = "PASS" if ok else "FAIL"
    print(f"  [{sym}] {name}: {status} {json.dumps(body)}{detail}")
    return ok

results = []

# --- Clamp tests ---
print("=== FW-2: samplePeriodUs clamp (0 -> 750) ===")
api("PATCH", "/settings", {"sampling": {"samplePeriodUs": 0}})
time.sleep(0.5)
s, b = api("GET", "/settings")
val = b.get("sampling",{}).get("samplePeriodUs")
results.append(test("samplePeriodUs min", s, {"val": val}, 200, {"val": 750}))

print("\n=== FW-2: samplePeriodUs high clamp (999999 -> 100000) ===")
api("PATCH", "/settings", {"sampling": {"samplePeriodUs": 999999}})
time.sleep(0.5)
s, b = api("GET", "/settings")
val = b.get("sampling",{}).get("samplePeriodUs")
results.append(test("samplePeriodUs max", s, {"val": val}, 200, {"val": 100000}))

# Restore
api("PATCH", "/settings", {"sampling": {"samplePeriodUs": 2000}})

print("\n=== FW-3: wsPublishMs clamp (0 -> 50) ===")
api("PATCH", "/settings", {"sampling": {"wsPublishMs": 0}})
time.sleep(0.5)
s, b = api("GET", "/settings")
val = b.get("sampling",{}).get("wsPublishMs")
results.append(test("wsPublishMs min", s, {"val": val}, 200, {"val": 50}))

print("\n=== FW-3: wsPublishMs high clamp (99999 -> 10000) ===")
api("PATCH", "/settings", {"sampling": {"wsPublishMs": 99999}})
time.sleep(0.5)
s, b = api("GET", "/settings")
val = b.get("sampling",{}).get("wsPublishMs")
results.append(test("wsPublishMs max", s, {"val": val}, 200, {"val": 10000}))

# Restore
api("PATCH", "/settings", {"sampling": {"wsPublishMs": 200}})

print("\n=== FW-6: rpmStableTol clamp (0 -> 10) ===")
api("PATCH", "/settings", {"motor": {"rpmStableTol": 0}})
time.sleep(0.5)
s, b = api("GET", "/settings")
val = b.get("motor",{}).get("rpmStableTol")
results.append(test("rpmStableTol min", s, {"val": val}, 200, {"val": 10}))

print("\n=== FW-6: rpmStableHoldMs clamp (0 -> 100) ===")
api("PATCH", "/settings", {"motor": {"rpmStableHoldMs": 0}})
time.sleep(0.5)
s, b = api("GET", "/settings")
val = b.get("motor",{}).get("rpmStableHoldMs")
results.append(test("rpmStableHoldMs min", s, {"val": val}, 200, {"val": 100}))

# Restore
api("PATCH", "/settings", {"motor": {"rpmStableTol": 120, "rpmStableHoldMs": 900}})

print("\n=== Phase offset moved to per-profile: PATCH /settings rejects deprecated field ===")
s, b = api("PATCH", "/settings", {"model": {"phaseOffsetDeg": 10}})
results.append(test("settings_phaseOffset_deprecated", s, b, 400, {"err": "deprecated_field", "field": "model.phaseOffsetDeg"}))

# --- FW-5: Snapshot lifecycle ---
print("\n=== FW-5: stale/no_results guard after aborted run ===")
api("POST", "/cmd/start_test", {"profileId": "1750"})
time.sleep(2)
api("POST", "/cmd/stop", {})
time.sleep(0.5)
s, b = api("POST", "/cmd/save_session", {"name": "test", "notes": ""})
guard_ok = s == 409 and b.get("err") in ("no_results", "stale_result")
results.append(test("save_guard_409", 200 if guard_ok else s, {"ok": guard_ok, "err": b.get("err")}, 200, {"ok": True}))

# --- Settings readback ---
print("\n=== Settings readback ===")
s, b = api("GET", "/settings")
results.append(test("settings_reachable", s, b, 200))
model = b.get("model", {})
phase_present = "phaseOffsetDeg" in model
results.append(test("settings_no_phaseOffsetDeg", 200, {"present": phase_present}, 200, {"present": False}))
print(f"  Current: samplePeriodUs={b.get('sampling',{}).get('samplePeriodUs')}, "
      f"wsPublishMs={b.get('sampling',{}).get('wsPublishMs')}, "
      f"rpmStableTol={b.get('motor',{}).get('rpmStableTol')}, "
      f"rpmStableHoldMs={b.get('motor',{}).get('rpmStableHoldMs')}")

# --- Profiles readback ---
print("\n=== Profiles readback ===")
s, b = api("GET", "/profiles")
results.append(test("profiles_reachable", s, b, 200))
phase_keys_ok = all("phaseOffsetDeg" in p for p in b.get("profiles", []))
results.append(test("profiles_have_phaseOffsetDeg", 200, {"ok": phase_keys_ok}, 200, {"ok": True}))
for p in b.get("profiles", []):
    print(f"  Profile: {p.get('id')} - {p.get('name')} ({p.get('rpm')} RPM) phaseOffsetDeg={p.get('phaseOffsetDeg')}")

print("\n=== Profile create without phaseOffsetDeg auto-seeds ===")
tmp_id = "seed_test_2750"
api("DELETE", f"/profiles/{tmp_id}")
s, b = api("POST", "/profiles", {
    "id": tmp_id,
    "name": "Seed Test 2750",
    "rpm": 2750,
    "spinupMs": 2500,
    "dwellMs": 3500,
    "repeats": 1
})
results.append(test("profile_create_seed", s, b, 200, {"ok": True}))
s, b = api("GET", "/profiles")
seeded = None
for p in b.get("profiles", []):
    if p.get("id") == tmp_id:
        seeded = p.get("phaseOffsetDeg")
        break
seed_ok = isinstance(seeded, (int, float))
results.append(test("profile_seed_value_present", 200, {"ok": seed_ok}, 200, {"ok": True}))
api("DELETE", f"/profiles/{tmp_id}")

# --- UI serving ---
print("\n=== UI serving (index.html) ===")
try:
    req = urllib.request.Request(f"{BASE}/")
    resp = urllib.request.urlopen(req, timeout=5)
    html = resp.read().decode()
    has_script = "<script" in html.lower()
    has_div = "root" in html
    ok = resp.status == 200 and has_script
    sym = "PASS" if ok else "FAIL"
    print(f"  [{sym}] index.html: {resp.status}, has script: {has_script}, has root: {has_div}, size: {len(html)} bytes")
    results.append(ok)
except Exception as e:
    print(f"  [FAIL] Could not load UI: {e}")
    results.append(False)

# --- WiFi status ---
print("\n=== WiFi status ===")
s, b = api("GET", "/wifi/status")
results.append(test("wifi_status", s, b, 200))
if s == 200:
    print(f"  Connected: {b.get('connected')}, IP: {b.get('ip')}, mDNS: {b.get('mdns')}")
    print(f"  AP Mode: {b.get('apMode')}, STA IP: {b.get('staIp')}, AP IP: {b.get('apIp')}")

print(f"\n{'='*50}")
print(f"RESULTS: {sum(results)}/{len(results)} passed")
if all(results):
    print("ALL TESTS PASSED")
else:
    print("SOME TESTS FAILED")
sys.exit(0 if all(results) else 1)
