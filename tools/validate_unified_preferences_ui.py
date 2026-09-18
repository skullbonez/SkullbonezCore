"""Verify editor viewport and Tools preferences across real process restarts."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from skarness import SkarnessConnection, launch
from validate_ui_themes import wait_for_exit

REPO = Path(__file__).resolve().parents[1]


def run(root: Path) -> None:
    root = root.resolve()
    root.mkdir(parents=True, exist_ok=False)
    preferences = root / 'ui-layout.preferences'
    legacy = ('version 5\nlayout 1\nleft 391\nright 450\ndrawer 422\ndiagnostics 140\n'
              'folded 0\ntool 3\nleftFolded 0\nrightFolded 0\ntheme 1\nreplayFolded 0\n')
    preferences.write_text(legacy)
    retained = {}
    checks = []

    def session(label: str, expected_open: bool, toggle: bool = False, invalid: bool = False) -> None:
        directory = root / label
        assert launch(directory, REPO / 'Automation/SKULLBONEZ_CORE.exe',
                      REPO / 'SkullbonezData/scenes/space_field_200.scene.json',
                      hidden=True, layout_file=preferences) == 0
        connection = SkarnessConnection(directory)
        latest = {}
        offset = 0

        def send(command: str, **args: object) -> dict:
            result = connection.wait(connection.send(command, args))
            assert result.get('status') == 'applied', (command, result)
            return result

        def sample(name: str) -> dict:
            nonlocal offset
            send('run.step_frames', count=4)
            with (directory / 'runtime.skarness.ndjson').open('rb') as stream:
                stream.seek(offset)
                for line in stream:
                    if not line.endswith(b'\n'):
                        break
                    offset += len(line)
                    row = json.loads(line)
                    if 'topic' in row:
                        latest[row['topic']] = row['payload']
            (directory / f'{name}.json').write_text(json.dumps(latest, indent=2))
            ui = latest['ui.presentation']
            assert ui['viewport'] == [int(v) for v in ui['editorCanvasBounds']], ui
            x, y, w, h = ui['viewport']
            if not invalid:
                assert x == 391 and w == ui['window'][0] - 391 - 450, ui['viewport']
                assert y == 42 and h < ui['window'][1] - y, ui['viewport']
            return ui

        try:
            assert {'input.pointer_drag', 'window.resize'} <= set(send('capabilities.get')['commands'])
            send('state.subscribe', topics=[], detail='normal')
            ui = sample('initial')
            assert ui['toolsVisible'] == expected_open, (label, ui['toolsVisible'])
            assert ui['layout'] == ('Canvas' if invalid else 'Editor'), ui['layout']
            assert ui['activeTool'] == (1 if invalid else 3), ui['activeTool']
            if not invalid:
                key = 'open' if expected_open else 'closed'
                if key in retained:
                    assert ui['viewport'] == retained[key], (label, retained, ui['viewport'])
                retained[key] = ui['viewport']
                send('ui.animation_clock', enabled=True, seconds=10.0)
                sample('settled')
                send('capture.screenshot', path=str(directory / 'startup.png'))
                # Native resize must preserve both dock widths and drawer state.
                send('window.resize', width=1280, height=800)
                resized = sample('resized')
                assert resized['toolsVisible'] == expected_open
                send('window.resize', width=ui['window'][0], height=ui['window'][1])
                ui = sample('size-restored')
            if toggle:
                x, y, w, h = ui['replayDetailsBounds']
                send('input.pointer_drag', button='left', x=int(x+w/2), y=int(y+h/2), deltaX=0, deltaY=0)
                ui = sample('tools-toggled')
                assert ui['toolsVisible'] != expected_open, ui
                retained['closed' if expected_open else 'open'] = ui['viewport']
            checks.append(label)
        finally:
            try:
                send('session.stop')
            finally:
                connection.close()
                wait_for_exit(directory)

    session('legacy-editor', False, toggle=True)
    assert 'version 7\n' in preferences.read_text() and 'toolsOpen 1\n' in preferences.read_text()
    session('restored-open', True)
    session('restored-open-then-close', True, toggle=True)
    assert 'toolsOpen 0\n' in preferences.read_text()
    session('restored-closed', False)
    preferences.write_text('version 999\nlayout 1\n')
    session('future-version', False, invalid=True)
    preferences.write_text(legacy.replace('version 5', 'version 6') + 'toolsOpen 9\n')
    session('invalid-tools-state', False, invalid=True)
    (root / 'result.json').write_text(json.dumps({'passed': True, 'checks': checks}, indent=2))
    print('PASS: editor viewport, Tools open/closed restart, resized layout and preference migrations')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, default=REPO / 'TestOutput/skarness/unified-preferences')
    run(parser.parse_args().session)
