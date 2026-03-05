#!/usr/bin/env python3
"""Capture WebSocket telemetry from the balance device during a test run."""
import asyncio, json, websockets, time, sys

WS_URI = "ws://192.168.4.165/ws"
TIMEOUT = int(sys.argv[1]) if len(sys.argv) > 1 else 50

async def monitor():
    async with websockets.connect(WS_URI, close_timeout=3) as ws:
        start = time.time()
        last_step = -1
        last_print = 0
        while time.time() - start < TIMEOUT:
            msg = await asyncio.wait_for(ws.recv(), timeout=5)
            d = json.loads(msg)
            t = d.get("telemetry", {})
            s = d.get("state", {})
            step = s.get("runStep", 0)
            motor = s.get("motorState", 0)
            elapsed = time.time() - start
            should_print = (step != last_step) or (elapsed - last_print >= 1.5)
            if should_print:
                rpm = t.get("rpm", 0)
                phase = t.get("phaseDeg", 0)
                vib = t.get("vibMag", 0)
                noise = t.get("noiseRms", 0)
                qual = t.get("quality", 0)
                heavy = t.get("heavyDeg", 0)
                add = t.get("addDeg", 0)
                led_on = t.get("ledOn", False)
                print(f"t={elapsed:5.1f}s  step={step} motor={motor}  RPM={rpm:7.1f}  phase={phase:6.1f}  vib={vib:.4f}g  noise={noise:.4f}g  q={qual:.2f}  heavy={heavy:6.1f}  add={add:6.1f}  led={'ON' if led_on else 'off'}")
                sys.stdout.flush()
                last_step = step
                last_print = elapsed
            if step == 3:
                print("=== TEST COMPLETE ===")
                msg2 = await asyncio.wait_for(ws.recv(), timeout=3)
                d2 = json.loads(msg2)
                t2 = d2.get("telemetry", {})
                print(f"FINAL: phaseDeg={t2.get('phaseDeg',0):.2f}  heavyDeg={t2.get('heavyDeg',0):.2f}  addDeg={t2.get('addDeg',0):.2f}  vibMag={t2.get('vibMag',0):.6f}g  quality={t2.get('quality',0):.3f}")
                sys.stdout.flush()
                break
            await asyncio.sleep(0.2)

asyncio.run(monitor())
