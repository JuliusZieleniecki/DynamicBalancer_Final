import websocket, json, time, threading

results = []
done = threading.Event()
test_started_time = None
saw_spinup = False

def on_message(ws, message):
    global test_started_time, saw_spinup
    data = json.loads(message)
    if data.get('type') == 'telemetry':
        t = data.get('telemetry', {})
        s = data.get('state', {})
        rpm = t.get('rpm', 0)
        runStep = s.get('runStep', -1)
        if not test_started_time:
            return
        if runStep == 1:
            saw_spinup = True
        elapsed = time.time() - test_started_time
        results.append({
            'rpm': rpm, 'runStep': runStep, 'elapsed': elapsed,
            'vibMag': t.get('vibMag', 0), 'phaseDeg': t.get('phaseDeg', 0),
            'quality': t.get('quality', 0), 'heavyDeg': t.get('heavyDeg', 0)
        })
        if saw_spinup and runStep == 3:
            done.set()
            ws.close()

ws = websocket.WebSocketApp('ws://192.168.4.165/ws', on_message=on_message)
t = threading.Thread(target=ws.run_forever)
t.daemon = True
t.start()
time.sleep(2)

import urllib.request, sys
profile_id = sys.argv[1] if len(sys.argv) > 1 else '2500'
req = urllib.request.Request(
    'http://192.168.4.165/cmd/start_test',
    data=json.dumps({'profileId': profile_id}).encode(),
    headers={'Content-Type': 'application/json'},
    method='POST'
)
urllib.request.urlopen(req, timeout=5)
test_started_time = time.time()
print(f'Test started: profile {profile_id}')

done.wait(timeout=60)

# Show first frames
for r in results[:3]:
    print(f"  t={r['elapsed']:.1f}s step={r['runStep']} rpm={r['rpm']:.0f}")
print("  ...")

# Last measure frames
measure_frames = [r for r in results if r['runStep'] == 2]
if measure_frames:
    for r in measure_frames[-3:]:
        print(f"  t={r['elapsed']:.1f}s step=2 rpm={r['rpm']:.0f} vib={r['vibMag']:.4f} phase={r['phaseDeg']:.1f} q={r['quality']:.2f}")

# Result
result_frames = [r for r in results if r['runStep'] == 3]
if result_frames:
    r = result_frames[-1]
    print(f"RESULT: rpm={r['rpm']:.0f} vibMag={r['vibMag']:.4f}g phaseDeg={r['phaseDeg']:.1f} heavyDeg={r['heavyDeg']:.1f} quality={r['quality']:.2f}")

# Step summary
steps = {}
for r in results:
    s = r['runStep']
    if s not in steps: steps[s] = []
    steps[s].append(r['rpm'])
for step, rpms in sorted(steps.items()):
    nz = [r for r in rpms if r > 100]
    avg_nz = sum(nz)/len(nz) if nz else 0
    print(f"  step={step}: {len(rpms)}f avg={avg_nz:.0f} min={min(rpms):.0f} max={max(rpms):.0f}")
