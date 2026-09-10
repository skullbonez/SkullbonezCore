"""Check Scene edge reveal, persistent Solver Lab chrome and its mouse exit."""
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
        with (session / 'runtime.skarness.ndjson').open() as stream:
            stream.seek(offset)
            for line in stream:
                row = json.loads(line)
                if 'topic' in row:
                    latest[row['topic']] = row['payload']
            offset = stream.tell()
        (session / (label + '.json')).write_text(json.dumps(latest, indent=2))
        return latest['ui.presentation']

    def hover(x: float, y: float, shown: bool, label: str) -> dict:
        send('input.pointer_position', x=int(x), y=int(y), enabled=True)
        ui = sample(label)
        path = session / (label + '.png')
        send('capture.screenshot', path=str(path))
        with Image.open(path) as image:
            rgb = image.convert('RGB')
            bx, by, bw, bh = ui['headerLayoutBounds']
            pixel = rgb.getpixel((int(bx + bw / 2), int(by + 3)))
            matches = all(abs(a - b) <= 2 for a, b in zip(pixel, (32, 43, 54)))
            assert matches == shown, (label, pixel, shown)
            if shown:
                assert rgb.getpixel((image.width // 2, 2)) == (23, 32, 41)
        return ui

    def click(x: float, y: float) -> None:
        send('input.pointer_drag', button='left', x=int(x), y=int(y), deltaX=0, deltaY=0)

    def press(name: str) -> None:
        bx, by, bw, bh = latest['ui.presentation'][name]
        click(bx + bw / 2, by + bh / 2)

    try:
        commands = send('capabilities.get')['commands']
        assert all(name in commands for name in ('input.pointer_position', 'input.pointer_drag', 'capture.screenshot'))
        send('state.subscribe', topics=[], detail='normal')
        ui = sample('initial')
        for layout in ('Canvas', 'Editor'):
            if ui['layout'] != layout:
                hover(ui['window'][0] - 110, 2, True, layout + '-edge-switch')
                press('headerLayoutBounds')
                ui = sample(layout + '-layout')
            assert ui['layout'] == layout
            width, height = ui['window']
            viewport = ui['viewport']
            camera = latest['camera.state']['selectedCameraHash']
            selection = dict(latest['selection.state'])
            pinned = layout == 'Editor'
            hover(width / 2, height / 2, pinned, layout + '-away')
            hover(width / 2, 1, True, layout + '-revealed')
            hover(width / 2, 30, True, layout + '-inside')
            ui = hover(width / 2, 100, pinned, layout + '-left')
            assert ui['viewport'] == viewport
            # A menu extends below the edge strip: its field stays drawn until
            # dismissal, without selecting a camera merely by hovering.
            x, _, w, _ = ui['cameraPopupBounds']
            hover(x + w / 2, 20, True, layout + '-camera-hover')
            click(x + w / 2, 20)
            ui = sample(layout + '-camera-open')
            assert ui['cameraPopupOpen'], ui
            ui = hover(x + w / 2, 60, True, layout + '-camera-menu')
            assert ui['cameraPopupOpen'], ui
            click(width / 2, height / 2)
            ui = hover(width / 2, height / 2, pinned, layout + '-dismissed')
            assert not ui['cameraPopupOpen']
            assert ui['viewport'] == viewport
            assert latest['camera.state']['selectedCameraHash'] == camera
            assert latest['selection.state'] == selection
            if layout == 'Editor':
                with Image.open(session / (layout + '-revealed.png')) as image:
                    rgb = image.convert('RGB')
                    assert rgb.getpixel((1, 120)) == (23, 32, 41)
        # Solver Lab stays visible through loading, Canvas, and playback.
        press('headerWorkspaceBounds')
        ui = sample('lab-open')
        assert ui['workspace'] == 'Solver Lab', ui
        press('headerCloseBounds')
        ui = sample('exit-initial-load')
        assert ui['workspace'] == 'Scene' and ui['viewport'] == [0, 0, width, height]
        assert not send('comparison.state')['result']['comparison']['loading']
        press('headerWorkspaceBounds')
        sample('workspace-switch')
        press('headerLayoutBounds')
        hover(width / 2, height / 2, True, 'lab-editor-away')
        hover(width / 2, 1, True, 'lab-revealed')
        hover(width / 2, 100, True, 'lab-editor-left')
        press('headerLayoutBounds')
        ui = sample('lab-canvas')
        assert ui['layout'] == 'Canvas'
        hover(width / 2, height / 2, True, 'lab-canvas-away')
        hover(width / 2, 1, True, 'lab-canvas-revealed')
        deadline = time.monotonic() + 90
        while True:
            comparison = send('comparison.state')['result']['comparison']
            assert not comparison['loadError'], comparison
            if comparison['active'] and not comparison['loading']:
                break
            assert time.monotonic() < deadline, comparison
            sample('lab-loading')
        send('comparison.play', direction=1)
        hover(width / 2, height / 2, True, 'lab-playing-away')
        assert sample('lab-content')['viewport'][1] == 42
        for layout in ('Canvas', 'Editor'):
            if layout == 'Editor':
                press('headerWorkspaceBounds')
                sample('workspace-switch')
                press('headerLayoutBounds')
            ui = hover(width / 2, height / 2, True, 'lab-exit-' + layout)
            assert ui['layout'] == layout and ui['workspace'] == 'Solver Lab'
            press('headerCloseBounds')
            ui = sample('closed-' + layout)
            assert ui['workspace'] == 'Scene' and ui['layout'] == 'Canvas'
            assert ui['viewport'] == [0, 0, width, height] and not ui['toolsVisible']
            retained = send('comparison.state')['result']['comparison']
            assert retained['active'] and retained['direction'] == 0
        press('headerWorkspaceBounds')
        sample('workspace-switch')
        send('window.resize', width=320, height=240)
        ui = hover(160, 120, True, 'lab-compact-away')
        assert ui['viewport'][1] == 36
        press('headerCloseBounds')
        ui = sample('closed-compact')
        assert ui['workspace'] == 'Scene' and ui['viewport'] == [0, 0, 320, 240]
        print('PASS: Scene header reveal, persistent Solver Lab loading/playback header, Canvas/Editor mouse exit and retained comparison')
    finally:
        try:
            send('session.stop')
        finally:
            connection.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    run(parser.parse_args().session)
