"""Drive compact Tools menus through the native pointer route at small sizes."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]

def run(session: Path) -> None:
    assert launch(session, REPO / 'Automation/SKULLBONEZ_CORE.exe',
                  REPO / 'SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json', hidden=True) == 0
    connection = SkarnessConnection(session)
    latest: dict = {}
    offset = 0
    def send(command: str, **args: object) -> dict:
        reply = connection.wait(connection.send(command, args))
        assert reply.get('status') == 'applied', reply
        return reply
    def sample(label: str) -> dict:
        nonlocal offset
        send('run.step_frames', count=3)
        with (session / 'runtime.skarness.ndjson').open() as trace:
            trace.seek(offset)
            for line in trace:
                row = json.loads(line)
                if 'topic' in row:
                    latest[row['topic']] = row['payload']
            offset = trace.tell()
        (session / (label + '.json')).write_text(json.dumps(latest, indent=2))
        return latest['ui.presentation']
    def click(x: float, y: float) -> None:
        send('input.pointer_drag', button='left', x=int(x), y=int(y), deltaX=0, deltaY=0)
    def select(ui: dict, index: int, label: str) -> dict:
        assert ui['toolsPopupOpen'], ui
        for attempt in range(15):
            first, count = ui['toolsPopupFirstOption'], ui['toolsPopupVisibleOptions']
            x, y, w, h = ui['toolsPopupBounds']
            assert x >= 0 and y >= 0 and x + w <= ui['window'][0] and y + h <= ui['window'][1], ui
            if first <= index < first + count:
                click(x + 30, y + (index - first + 0.5) * h / count)
                return sample(label)
            send('input.pointer_wheel', x=int(x + 20), y=int(y + 10), wheelDelta=-120 if index >= first + count else 120)
            ui = sample(label + '-scroll-' + str(attempt))
        raise AssertionError('Popup option was not reachable')
    try:
        capabilities = send('capabilities.get')['commands']
        assert {'window.resize', 'input.pointer_wheel', 'input.set_focus'} <= set(capabilities)
        send('state.subscribe', topics=[], detail='normal')
        ui = sample('initial')
        click(ui['window'][0] - 30, 20)
        for layout in ('Canvas', 'Editor'):
            send('window.resize', width=1784, height=961)
            ui = sample(layout + '-wide')
            if ui['layout'] != layout:
                click(1784 - 110, 20)
                ui = sample(layout + '-switch')
            for width, height in ((640,480), (480,360), (320,240)):
                prefix = f'{width}x{height}-{layout}'
                send('window.resize', width=width, height=height)
                ui = sample(prefix + '-initial')
                assert ui['toolsVisible'] and ui['layout'] == layout, ui
                top = ui['viewport'][1] + ui['viewport'][3]
                if layout == 'Editor':
                    top += ui['transportBounds'][3]
                # The shell rounds drawer placement to native client pixels.
                top = int(top)
                for index in range(11):
                    click(width / 2, top + 40)
                    ui = select(sample(prefix + '-menu-' + str(index)), index, prefix + '-tab-' + str(index))
                    assert ui['activeTool'] == index and not ui['toolsPopupOpen'], ui
                send('capture.screenshot', path=str((session / (prefix + '-memory.png')).resolve()))
                # Floating diagnostics reserve no footer space.
                bottom = height
                click(width / 2, bottom - 14)
                ui = sample(prefix + '-display-menu')
                assert ui['toolsPopupOptions'] == 7 and ui['toolsPopupOpen'], ui
                send('capture.screenshot', path=str((session / (prefix + '-display.png')).resolve()))
                before = ui['profilerTimeline']
                ui = select(ui, 6, prefix + '-timeline')
                assert ui['profilerTimeline'] != before, ui
                click(width / 2, bottom - 14)
                ui = select(sample(prefix + '-display-reflection'), 1, prefix + '-reflection')
                assert ui['toolsPopupOpen'] and ui['toolsPopupOptions'] == 3, ui
                send('capture.screenshot', path=str((session / (prefix + '-reflection.png')).resolve()))
                ui = select(ui, 2, prefix + '-reflection-none')
                assert not ui['toolsPopupOpen'], ui
                click(width / 2, top + 40)
                assert sample(prefix + '-focus-popup')['toolsPopupOpen']
                send('input.set_focus', focused=False)
                ui = sample(prefix + '-focus-lost')
                assert not ui['toolsPopupOpen'], ui
                send('input.set_focus', focused=True)
                if layout == 'Editor':
                    send('input.set_key', key=0x74, down=True)
                    sample(prefix + '-restore-f5')
                    send('input.set_key', key=0x74, down=False)
                    ui = sample(prefix + '-release-f5')
                    assert ui['markerHistoryVisible'] and not ui['memoryWaterlineVisible']
                    hx, hy, hw, hh = ui['markerHistoryBounds']
                    selector_y = hy + 32
                    click(int(hx+30), int(selector_y+10))
                    ui = sample(prefix + '-marker-menu')
                    send('capture.screenshot', path=str((session / (prefix + '-marker-menu.png')).resolve()))
                    room = max(selector_y-8-4, height-8-selector_y-28)
                    rows = min(8, max(1, int((room-4-16)/22)))
                    popup_h = 4 + rows*22 + 16
                    popup_y = selector_y+28
                    if popup_y+popup_h > height-8:
                        popup_y = selector_y-popup_h-4
                    before = ui['markerSelectionHash']
                    active = ui['activeTool']
                    click(int(hx+30), int(popup_y+13))
                    ui = sample(prefix + '-marker-selected')
                    assert ui['markerSelectionHash'] != before, ui
                    assert ui['activeTool'] == active and ui['toolsVisible'], ui
                    send('input.set_focus', focused=False)
                    sample(prefix + '-marker-dismissed')
                    send('input.set_focus', focused=True)
                    send('input.set_key', key=0x74, down=True)
                    sample(prefix + '-hide-f5')
                    send('input.set_key', key=0x74, down=False)
                    sample(prefix + '-hide-f5-release')
        print('PASS: all eleven native Tools tabs, clipped scrollable menus, footer timeline/reflection and focus loss in both small layouts')
    finally:
        try:
            send('session.stop')
        finally:
            connection.close()

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--session', type=Path, required=True)
    run(parser.parse_args().session.resolve())
