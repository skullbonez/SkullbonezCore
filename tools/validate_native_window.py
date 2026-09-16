"""Exercise native capture release and Windows caption commands through Skarness."""
import argparse, json
from pathlib import Path
from skarness import launch, SkarnessConnection
from validate_ui_themes import wait_for_exit
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--session', type=Path, required=True)
root = parser.parse_args().session.resolve()
root.mkdir(parents=True, exist_ok=False)
repo = Path(__file__).resolve().parents[1]
prefs = root / 'layout.preferences'
prefs.write_text('version 6\nlayout 1\nleft 280\nright 360\ndrawer 360\ndiagnostics 140\nfolded 7\ntool 3\nleftFolded 0\nrightFolded 0\ntheme 0\nreplayFolded 0\ntoolsOpen 0\n')
launch(root, repo / 'Automation/SKULLBONEZ_CORE.exe', repo / 'SkullbonezData/scenes/space_field_200.scene.json', hidden=True, layout_file=prefs)
c = SkarnessConnection(root)
latest = {}
offset = 0
checks = []

def send(cmd, **args):
    r = c.wait(c.send(cmd, args))
    assert r['status'] == 'applied', r
    return r

def sample(name):
    global offset
    send('run.step_frames', count=3)
    with (root / 'runtime.skarness.ndjson').open('rb') as f:
        f.seek(offset)
        for line in f:
            if not line.endswith(b'\n'):
                break
            offset += len(line)
            r = json.loads(line)
            if 'topic' in r:
                latest[r['topic']] = r['payload']
    (root / (name + '.json')).write_text(json.dumps(latest, indent=2))
    checks.append((name, latest['input.state']))
    print(checks[-1])
    assert not latest['input.state']['nativeCaptureRequested'], (name, latest['input.state'])
    assert not latest['input.state']['nativeMouseCaptured'], (name, latest['input.state'])
    return latest['ui.presentation']

def drag(bounds, button='left', dx=0, dy=0):
    x, y, w, h = bounds
    send('input.pointer_drag', button=button, x=int(x + w / 2), y=int(y + h / 2), deltaX=dx, deltaY=dy, moveClient=True)
closed = False
try:
    assert {'input.pointer_drag', 'window.set_maximized', 'window.close'} <= set(send('capabilities.get')['commands'])
    send('state.subscribe', topics=[], detail='normal')
    u = sample('initial')
    drag(u['viewport'], button='right', dx=35, dy=20)
    u = sample('camera-released')
    drag(u['transportBounds'], dx=100)
    u = sample('timeline-released')
    drag(u['replayDetailsBounds'])
    u = sample('tools-open')
    x, y, w, h = u['drawerBounds']
    drag([x, y, w, 4], dy=-40)
    u = sample('drawer-released')
    drag(u['leftResizeBounds'], dx=25)
    u = sample('dock-released')
    original_size = u['window']
    for maximized in (True, False, True, False):
        send('window.set_maximized', maximized=maximized)
        u = sample('maximized-' + str(maximized))
        assert latest['input.state']['windowMaximized'] == maximized
        if not maximized:
            assert u['window'] == original_size
    send('capture.screenshot', path=str(root / 'restored.png'))
    rows = [json.loads(line) for line in (root / 'runtime.skarness.ndjson').read_text().splitlines()]
    assert any((r.get('topic') == 'input.state' and r['payload']['nativeMouseCaptured'] for r in rows)), 'Drag never acquired real HWND capture'
    send('window.close')
    closed = True
    wait_for_exit(root)
    (root / 'result.json').write_text(json.dumps({'passed': True, 'checks': checks, 'nativeClose': True}, indent=2))
    print('PASS: held gestures acquire capture; release frees it; native maximize, restore and close succeed')
finally:
    try:
        if not closed:
            send('session.stop')
    finally:
        c.close()
        wait_for_exit(root)
