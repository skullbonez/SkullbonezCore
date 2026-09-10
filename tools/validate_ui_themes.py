"""Check live theme changes, legacy migration and persistence through native Tools."""
from __future__ import annotations
import argparse
import ctypes
from ctypes import wintypes
import json
import time
from pathlib import Path
from PIL import Image
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def wait_for_exit(directory: Path) -> None:
    kernel = ctypes.windll.kernel32
    kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    handle = kernel.OpenProcess(0x00100000, False, json.loads((directory/'session.json').read_text())['processId'])
    if handle:
        try:
            assert kernel.WaitForSingleObject(handle, 10000) == 0
        finally:
            kernel.CloseHandle(handle)


def run(root: Path) -> None:
    root = root.resolve()
    root.mkdir(parents=True, exist_ok=False)
    prefs = root / 'ui-layout.preferences'
    legacy = 'version 1\nlayout 1\nleft 280\nright 360\ndrawer 480\ndiagnostics 140\nfolded 7\ntool 4\nleftFolded 0\nrightFolded 0\n'
    prefs.write_text(legacy)

    def session(label: str, expected: int, exercise: bool = False) -> None:
        directory = root / label
        assert launch(directory, REPO/'Automation/SKULLBONEZ_CORE.exe',
                      REPO/'SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json',
                      hidden=True, layout_file=prefs) == 0
        connection = SkarnessConnection(directory)
        latest: dict = {}
        offset = 0
        def send(command: str, **args: object) -> dict:
            result = connection.wait(connection.send(command, args))
            assert result.get('status') == 'applied', result
            return result
        def sample(name: str) -> dict:
            nonlocal offset
            # Pointer checks target settled controls; transitions have their own clock-pinned gate.
            deadline = time.monotonic() + .2
            while time.monotonic() < deadline:
                send('run.step_frames', count=3)
            with (directory/'runtime.skarness.ndjson').open() as stream:
                stream.seek(offset)
                for line in stream:
                    row = json.loads(line)
                    if 'topic' in row: latest[row['topic']] = row['payload']
                offset = stream.tell()
            (directory/(name+'.json')).write_text(json.dumps(latest, indent=2))
            return latest['ui.presentation']
        def click(x: float, y: float) -> None:
            send('input.pointer_drag', button='left', x=int(x), y=int(y), deltaX=0, deltaY=0)
        def capture(name: str) -> Path:
            path = directory/(name+'.png')
            send('input.pointer_position', x=800, y=180, enabled=True)
            send('capture.screenshot', path=str(path))
            return path
        try:
            assert {'input.pointer_drag', 'capture.screenshot'} <= set(send('capabilities.get')['commands'])
            send('state.subscribe', topics=[], detail='normal')
            ui = sample('initial')
            assert ui['theme'] == expected and ui['layout'] == 'Editor' and ui['activeTool'] == 4, ui
            assert not ui['toolsVisible']
            if not exercise:
                capture('restored')
                if label == 'reload':
                    for key in (0x74,0x75):
                        send('input.set_key', key=key, down=True)
                        sample('diagnostic-key')
                        send('input.set_key', key=key, down=False)
                    sample('diagnostics')
                    capture('light-diagnostics')
                    for key in (0x74,0x75):
                        send('input.set_key', key=key, down=True)
                        sample('diagnostic-close-key')
                        send('input.set_key', key=key, down=False)
                    send('comparison.load', path=str(REPO/'SkullbonezData/solver-lab/wall-only/comparison.json'))
                    ui = sample('lab')
                    assert ui['theme'] == 2 and ui['workspace'] == 'Solver Lab'
                    capture('light-solver-lab')
                return
            settle = time.monotonic() + .5
            while time.monotonic() < settle:
                send('run.step_frames', count=20)
            # The legacy layout still opens the same drawer and Options tab.
            x,y,w,h = ui['replayDetailsBounds']
            click(x+w/2,y+h/2)
            ui = sample('options')
            assert ui['toolsVisible'] and ui['activeTool'] == 4, ui
            for theme,name,rgb in ((1,'Dark',(27,27,29)), (2,'Light',(241,243,246)), (0,'Blue',(23,32,41)), (2,'Light-final',(241,243,246))):
                x,y,w,h = ui['toolsContentBounds']
                if y+314-ui['toolsScroll'] >= y+h:
                    send('input.pointer_wheel', x=int(x+50), y=int(y+h/2), wheelDelta=-600)
                    ui = sample('scrolled')
                bw = (min(w,420)-12)/3
                click(x+theme*(bw+6)+bw/2, y+314-ui['toolsScroll'])
                ui = sample(name)
                assert ui['theme'] == theme, ui
                image_path = capture(name)
                with Image.open(image_path) as image:
                    pixel = image.convert('RGB').getpixel((image.width//2,2))
                    assert max(abs(a-b) for a,b in zip(pixel,rgb)) <= 3, (name,pixel,rgb)
            for index in range(11):
                click(14+(ui['window'][0]-28)*(index+.5)/11, ui['drawerBounds'][1]+66)
                ui = sample('light-tool-'+str(index))
                assert ui['activeTool'] == index
                capture('light-tool-'+str(index))
            click(14+(ui['window'][0]-28)*4.5/11, ui['drawerBounds'][1]+66)
            ui = sample('options-restored')
            # A theme change never resets the dock layout or selected tool.
            assert ui['layout'] == 'Editor' and ui['activeTool'] == 4
        finally:
            try: send('session.stop')
            finally: connection.close()
            wait_for_exit(directory)
    session('live', 0, True)
    saved = prefs.read_text()
    assert 'version 3\n' in saved and 'theme 2\n' in saved and 'drawer 480\n' in saved, saved
    session('reload', 2)
    prefs.write_text(saved.replace('theme 2','theme 999'))
    session('unknown-theme', 0)
    print('PASS: native Blue/Dark/Light switching and pixels, version 1 layout migration, restart persistence, invalid theme fallback')

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    run(parser.parse_args().session)
