"""Exercise vertical dock tabs, full-screen switching and the draggable Tools tab."""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

from PIL import Image
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    session = session.resolve()
    assert launch(session, REPO / 'Automation/SKULLBONEZ_CORE.exe',
                  REPO / 'SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json', hidden=True) == 0
    connection = SkarnessConnection(session)
    latest: dict = {}
    offset = 0

    def send(command: str, **args: object) -> dict:
        result = connection.wait(connection.send(command, args))
        assert result.get('status') == 'applied', result
        return result

    def sample(label: str) -> dict:
        nonlocal offset
        send('run.step_frames', count=3)
        with (session / 'runtime.skarness.ndjson').open(encoding='utf-8') as stream:
            stream.seek(offset)
            for line in stream:
                event = json.loads(line)
                if 'topic' in event:
                    latest[event['topic']] = event['payload']
            offset = stream.tell()
        (session / f'{label}.json').write_text(json.dumps(latest, indent=2), encoding='utf-8')
        return latest['ui.presentation']

    def click(bounds: list) -> None:
        x, y, w, h = bounds
        assert w > 0 and h > 0, bounds
        send('input.pointer_drag', button='left', x=int(x+w/2), y=int(y+h/2), deltaX=0, deltaY=0)

    def capture(label: str) -> Path:
        send('input.pointer_position', x=800, y=450, enabled=True)
        path = session / f'{label}.png'
        send('capture.screenshot', path=str(path))
        return path

    def vertical(path: Path, bounds: list, color: tuple) -> None:
        x, y, w, h = bounds
        with Image.open(path) as image:
            rgb = image.convert('RGB')
            glyphs = [(px, py) for py in range(int(y+4), int(y+h-4))
                      for px in range(int(x+4), int(x+w-4))
                      if max(abs(a-b) for a, b in zip(rgb.getpixel((px, py)), color)) < 45]
        assert glyphs, (path, bounds)
        # Exclude the colored tab border: the interior glyphs must run down the rail.
        assert max(p[1] for p in glyphs)-min(p[1] for p in glyphs) > 24, (path, glyphs)

    def fullscreen(ui: dict) -> None:
        assert ui['layout'] == 'Canvas' and not ui['toolsVisible'], ui
        assert ui['viewport'] == [0, 0, *ui['window']], ui

    try:
        commands = send('capabilities.get')['commands']
        assert {'input.pointer_drag', 'input.set_key', 'window.resize', 'comparison.state'} <= set(commands)
        send('state.subscribe', topics=[], detail='normal')
        ui = sample('initial')
        fullscreen(ui)
        click(ui['headerLayoutBounds'])
        ui = sample('docked-collapsed')
        assert ui['layout'] == 'Editor'
        assert ui['editorControlsBounds'][2] == 0 and ui['replayControlsBounds'][2] == 0
        assert ui['causeControlsBounds'][2] == 0
        path = capture('scene-vertical-tabs')
        vertical(path, ui['editorTabBounds'], (255, 117, 31))
        vertical(path, ui['editorReplayTabBounds'], (26, 209, 122))
        vertical(path, ui['causeTabBounds'], (0, 174, 239))
        click(ui['editorTabBounds'])
        ui = sample('editor-open')
        assert ui['editorControlsBounds'][2] > 0 and ui['causeControlsBounds'][2] == 0
        click(ui['causeTabBounds'])
        ui = sample('editor-and-causes')
        assert ui['editorControlsBounds'][2] > 0 and ui['causeControlsBounds'][2] > 0
        click(ui['editorReplayTabBounds'])
        ui = sample('replay-and-causes')
        assert ui['replayControlsBounds'][2] > 0 and ui['editorControlsBounds'][2] == 0
        click(ui['leftFoldBounds'])
        click(ui['rightFoldBounds'])
        ui = sample('folded-again')
        x, y, w, h = ui['replayDetailsBounds']
        send('input.pointer_drag', button='left', x=int(x+w/2), y=int(y+h/2),
             deltaX=0, deltaY=-300, moveClient=True)
        ui = sample('tools-dragged-open')
        assert ui['toolsVisible'] and abs(ui['drawerBounds'][3]-300) <= 4, ui
        capture('tools-dragged-open')
        dx, dy, dw, dh = ui['drawerBounds']
        send('input.pointer_drag', button='left', x=int(dx+dw/2), y=int(dy+3),
             deltaX=0, deltaY=-160, moveClient=True)
        ui = sample('tools-resized-up')
        assert abs(ui['drawerBounds'][3]-460) <= 4, ui
        assert ui['viewport'][3] < dy, ui
        click(ui['headerLayoutBounds'])
        ui = sample('full-screen-from-tools')
        fullscreen(ui)
        click(ui['headerLayoutBounds'])
        ui = sample('docked-restored')
        assert ui['editorControlsBounds'][2] == 0 and ui['causeControlsBounds'][2] == 0
        click(ui['replayDetailsBounds'])
        ui = sample('tools-click-open')
        assert ui['toolsVisible'] and abs(ui['drawerBounds'][3]-460) <= 4
        click(ui['replayDetailsBounds'])
        ui = sample('tools-click-close')
        assert not ui['toolsVisible']
        click(ui['headerWorkspaceBounds'])
        deadline = time.monotonic()+90
        while True:
            ui = sample('lab-loading')
            comparison = send('comparison.state')['result']['comparison']
            assert not comparison['loadError'], comparison
            if comparison['active'] and not comparison['loading']:
                assert comparison['bundle'].replace('\\', '/').endswith('ragdoll-wall/comparison.json')
                break
            assert time.monotonic() < deadline
        ui = sample('lab-folded')
        assert ui['workspace'] == 'Solver Lab' and ui['layout'] == 'Editor'
        path = capture('lab-vertical-tabs')
        vertical(path, ui['editorTabBounds'], (255, 117, 31))
        vertical(path, ui['causeTabBounds'], (0, 174, 239))
        click(ui['editorTabBounds'])
        ui = sample('lab-controls-open')
        assert ui['replayControlsBounds'][2] > 0
        click(ui['causeTabBounds'])
        ui = sample('lab-differences-open')
        assert ui['causeControlsBounds'][2] > 0
        capture('lab-open-panels')
        click(ui['leftFoldBounds'])
        click(ui['rightFoldBounds'])
        send('window.resize', width=320, height=240)
        ui = sample('lab-compact-folded')
        capture('lab-compact-folded')
        click(ui['editorTabBounds'])
        ui = sample('lab-compact-controls')
        assert ui['replayControlsBounds'][2] > 0
        click(ui['headerCloseBounds'])
        ui = sample('lab-exited')
        fullscreen(ui)
        print('PASS: vertical Scene/Lab tabs, selected panels, full-screen toggle and upward Tools drag/resize/retention')
    finally:
        try:
            send('session.stop')
        finally:
            connection.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    run(parser.parse_args().session)
