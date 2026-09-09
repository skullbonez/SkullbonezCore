"""Verify native Scene timing controls against Scene, Prediction and Planning owners."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]

def run(session: Path) -> None:
    assert launch(session, REPO / 'Automation/SKULLBONEZ_CORE.exe',
                  REPO / 'SkullbonezData/scenes/solar_system.scene.json', hidden=True) == 0
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
        with (session / 'runtime.skarness.ndjson').open() as trace:
            trace.seek(offset)
            for line in trace:
                row = json.loads(line)
                if 'topic' in row:
                    latest[row['topic']] = row['payload']
            offset = trace.tell()
        (session / (label + '.json')).write_text(json.dumps(latest, indent=2))
        return latest['ui.presentation']
    def click(x: float, y: float, hold: int = 0) -> None:
        send('input.pointer_drag', button='left', x=int(x), y=int(y), deltaX=0, deltaY=0, holdMilliseconds=hold)
    def scroll(top: float, steps: int) -> None:
        send('input.pointer_wheel', x=300, y=int(top + 220), wheelDelta=12000)
        if steps:
            send('input.pointer_wheel', x=300, y=int(top + 220), wheelDelta=-120 * steps)
    def prediction() -> dict:
        return latest['replay.prediction.controls']
    try:
        assert 'input.pointer_drag' in send('capabilities.get')['commands']
        send('state.subscribe', topics=[], detail='normal')
        ui = sample('initial')
        width, height = ui['window']
        for layout in ('Canvas', 'Editor'):
            if ui['layout'] != layout:
                click(width - 110, 20)
                ui = sample(layout + '-layout')
            click(width - 189, 20)
            ui = sample(layout + '-scene-open')
            top = ui['viewport'][1] + ui['viewport'][3] + (28 if layout == 'Editor' else 0)
            assert ui['activeTool'] == 1 and ui['toolsVisible']
            scroll(top, 20)
            sample(layout + '-playback-controls')
            # Default drawer height 360 leaves 176 content pixels; scrolling
            # reaches the final pause/step row in both perimeter arrangements.
            row = top + 100 + 516 - (548 - 176) + 12
            if not latest['scene.state']['pauseLocked']:
                click(120, row)
                sample(layout + '-pause-on')
            assert latest['scene.state']['pauseLocked']
            before = latest['frame.clocks']['sceneFrame']
            # Skarness' own pause would override a scene step. Release only
            # that harness pause; the real Scene pause lock stays enabled.
            send('run.resume')
            click(width * 0.75, row, hold=150)
            ui = sample(layout + '-single-step')
            assert latest['frame.clocks']['sceneFrame'] == before + 1, latest['frame.clocks']
            send('run.step_frames', count=12)
            sample(layout + '-single-step-held-result')
            assert latest['frame.clocks']['sceneFrame'] == before + 1
            send('capture.screenshot', path=str((session / (layout + '-playback-controls.png')).resolve()))
            click(120, row)
            sample(layout + '-pause-off')
            assert not latest['scene.state']['pauseLocked']
            click(120, row)
            ui = sample(layout + '-pause-restored')
            assert latest['scene.state']['pauseLocked']
            scroll(top, 6)
            sample(layout + '-reveal-controls')
            before = latest['frame.clocks']['sceneFrame']
            for x, expected, label in ((25, 1, 'slow'), (width * 0.5, None, 'middle'), (width - 30, 1000, 'instant')):
                click(x, top + 100 + 268 - 252 + 17)
                sample(layout + '-reveal-' + label)
                rate = prediction()['revealRate']
                assert rate == expected if expected is not None else 1 < rate < 1000, prediction()
                assert latest['frame.clocks']['sceneFrame'] == before
            toggle_x = 100
            reset_x = 18 + (width - 44) * 0.83
            forecast_y = top + 100 + 344 - 252 + 12
            click(toggle_x, forecast_y)
            sample(layout + '-forecast-start')
            assert prediction()['forecastActive'] and prediction()['forecastAvailable'] and not prediction()['forecastFailed'], prediction()
            send('run.step_frames', count=60)
            sample(layout + '-forecast-progress')
            before_tick = prediction()['forecastNewestTick']
            assert before_tick > 0, prediction()
            assert latest['frame.clocks']['sceneFrame'] == before
            click(reset_x, forecast_y)
            sample(layout + '-forecast-reset')
            assert prediction()['forecastActive'] and prediction()['forecastNewestTick'] < before_tick, prediction()
            send('capture.screenshot', path=str((session / (layout + '-forecast.png')).resolve()))
            click(toggle_x, forecast_y)
            ui = sample(layout + '-forecast-stop')
            assert not prediction()['forecastActive'], prediction()
        print('PASS: native Scene pause/one-shot stepping, reveal speed endpoints/interior, and private forecast start/progress/reset/stop in both layouts')
    finally:
        try:
            send('session.stop')
        finally:
            connection.close()

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--session', type=Path, required=True)
    run(parser.parse_args().session.resolve())
