"""WS telemetry stability test - check for NaN, garbage, torn reads."""
import websocket
import json
import time
import math
import sys

WS_URL = "ws://balance.local/ws"
SAMPLE_COUNT = 50

print(f"=== WS Telemetry Stability Test ({SAMPLE_COUNT} samples) ===\n")

messages = []
errors = []

def on_message(ws, message):
    try:
        data = json.loads(message)
    except json.JSONDecodeError:
        errors.append(f"JSON decode error: {message[:100]}")
        return
    
    messages.append(data)
    
    if data.get("type") == "telemetry":
        t = data.get("telemetry", {})
        s = data.get("state", {})
        
        # Check for NaN/Inf in numeric fields
        for key in ["rpm", "vibMag", "phaseDeg", "quality", "noiseRms"]:
            val = t.get(key)
            if val is not None and isinstance(val, float):
                if math.isnan(val) or math.isinf(val):
                    errors.append(f"BAD VALUE: telemetry.{key} = {val}")
        
        # Check state fields are valid enums
        ms = s.get("motorState")
        rs = s.get("runStep")
        if ms not in (0, 1, 2):
            errors.append(f"BAD motorState: {ms}")
        if rs not in (0, 1, 2, 3):
            errors.append(f"BAD runStep: {rs}")

        # New state fields for per-profile phase offset lifecycle
        if not isinstance(s.get("phaseGuidanceStale", False), bool):
            errors.append(f"BAD phaseGuidanceStale type: {type(s.get('phaseGuidanceStale'))}")
        if "activeProfileId" in s and not isinstance(s.get("activeProfileId"), str):
            errors.append(f"BAD activeProfileId type: {type(s.get('activeProfileId'))}")
        apo = s.get("activeProfilePhaseOffsetDeg")
        if apo is not None and not isinstance(apo, (int, float)):
            errors.append(f"BAD activeProfilePhaseOffsetDeg type: {type(apo)}")
        if not isinstance(s.get("hasResultSnapshot", False), bool):
            errors.append(f"BAD hasResultSnapshot type: {type(s.get('hasResultSnapshot'))}")
        
        # Check quality is in range
        q = t.get("quality", 0)
        if isinstance(q, (int, float)) and (q < 0 or q > 1.001):
            errors.append(f"quality out of range: {q}")
        
        # Check phaseDeg is in range
        pd = t.get("phaseDeg", 0)
        if isinstance(pd, (int, float)) and (pd < 0 or pd > 360.001):
            errors.append(f"phaseDeg out of range: {pd}")
    
    if len(messages) >= SAMPLE_COUNT:
        ws.close()

def on_error(ws, error):
    errors.append(f"WS error: {error}")

def on_open(ws):
    print("  WebSocket connected")

def on_close(ws, close_status_code, close_msg):
    pass

ws = websocket.WebSocketApp(WS_URL,
                            on_open=on_open,
                            on_message=on_message,
                            on_error=on_error,
                            on_close=on_close)

ws.run_forever(ping_timeout=10)

print(f"\n  Received {len(messages)} messages")

# Analyze
telem_msgs = [m for m in messages if m.get("type") == "telemetry"]
print(f"  Telemetry messages: {len(telem_msgs)}")

if telem_msgs:
    # Print a sample
    sample = telem_msgs[-1]
    t = sample.get("telemetry", {})
    s = sample.get("state", {})
    print(f"\n  Last telemetry sample:")
    print(f"    rpm={t.get('rpm')}, vibMag={t.get('vibMag')}, phaseDeg={t.get('phaseDeg')}")
    print(f"    quality={t.get('quality')}, noiseRms={t.get('noiseRms')}")
    print(f"    motorState={s.get('motorState')}, runStep={s.get('runStep')}")
    print(f"    profileName={s.get('profileName')}")

if errors:
    print(f"\n  ERRORS ({len(errors)}):")
    for e in errors:
        print(f"    {e}")
    print("\n  [FAIL] WS telemetry has issues!")
    sys.exit(1)
else:
    print(f"\n  [PASS] All {len(telem_msgs)} telemetry messages clean - no NaN, no garbage, no torn reads")
    sys.exit(0)
