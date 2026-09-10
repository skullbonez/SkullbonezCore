"""Verify Canvas edge reveal, persistent Editor transport and paused replay."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from PIL import Image, ImageChops
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

    def hover(x: float, y: float, visible: bool, label: str) -> dict:
        send('input.pointer_position', x=int(x), y=int(y), enabled=True)
        # Fade uses wall time while the harness holds the simulation clock.
        time.sleep(0.7)
        ui = sample(label)
        assert latest['replay.timeline']['scrubber']['visible'] == visible, (label, latest['replay.timeline']['scrubber'])
        send('capture.screenshot', path=str(session / (label + '.png')))
        return ui

    def click(x: float, y: float) -> None:
        send('input.pointer_drag', button='left', x=int(x), y=int(y), deltaX=0, deltaY=0)

    try:
        capabilities = send('capabilities.get')['commands']
        assert all(name in capabilities for name in ('input.pointer_position', 'input.pointer_drag', 'capture.screenshot'))
        send('state.subscribe', topics=[], detail='normal')
        ui = sample('initial')
        # Build actual history so scrubbing must change the presented time.
        send('run.step', count=100)
        ui = sample('history')
        for layout in ('Canvas', 'Editor', 'Canvas'):
            if ui['layout'] != layout:
                click(ui['window'][0] - 110, 20)
                ui = sample(layout + '-layout')
            width, height = ui['window']
            tx, ty, tw, th = ui['transportBounds']
            vx, vy, vw, vh = ui['viewport']
            away_x, away_y = vx + vw / 2, vy + vh / 2
            pinned = layout == 'Editor'
            hover(away_x, away_y, pinned, layout + '-away')
            hover(tx + tw / 2, ty + th / 2, True, layout + '-revealed')
            if not pinned:
                with Image.open(session / (layout + '-away.png')) as hidden, Image.open(session / (layout + '-revealed.png')) as shown:
                    strip = (int(tx), int(ty), int(tx + tw + 72), int(ty + th))
                    assert ImageChops.difference(hidden.convert('RGB').crop(strip), shown.convert('RGB').crop(strip)).getbbox() is not None
            # The whole edge reveals, including the Details button and outside
            # the centered Canvas strip, without requiring a click.
            hover(width - 2, ty + th / 2, True, layout + '-edge')
            position = latest['replay.timeline']['scrubber']['position']
            click(width - 2, ty + th / 2)
            sample(layout + '-edge-click')
            assert latest['replay.timeline']['scrubber']['position'] == position
            hover(tx + tw + 30, ty + th / 2, True, layout + '-details-hover')
            before_scrub = latest['replay.timeline']['scrubber']['position']
            start = tx + 72 + (tw - 84) * 0.7
            send('input.pointer_drag', button='left', x=int(start), y=int(ty + th / 2),
                 deltaX=-80, deltaY=-150, moveClient=True, holdMilliseconds=400)
            ui = sample(layout + '-scrubbed')
            assert latest['input.state']['scrubPaused'], latest['input.state']
            assert latest['replay.timeline']['scrubber']['visible'], 'scrub capture faded during the hold outside the strip'
            position = latest['replay.timeline']['scrubber']['position']
            assert position < 0.8 and abs(position - before_scrub) > 0.01, latest['replay.timeline']['scrubber']
            hover(away_x, away_y, pinned, layout + '-paused-away')
            hover(tx + tw / 2, ty + th / 2, True, layout + '-paused-revealed')
            # Bind the rendered handle to the timeline position changed by the
            # drag. A state-only check misses a handle drawn from another track.
            inset = min(72.0, tw * 0.22)
            knob_x = tx + inset + (tw - inset - 12) * position
            with Image.open(session / (layout + '-paused-revealed.png')) as image:
                pixel = image.convert('RGB').getpixel((round(knob_x), round(ty + th / 2)))
                assert min(pixel) > 220, (layout, 'handle is not at the dragged position', position, knob_x, pixel)
            for distance in (-80, 100):
                destination = (knob_x + distance - tx - inset) / (tw - inset - 12)
                send('input.pointer_drag', button='left', x=round(knob_x), y=round(ty + th / 2),
                     deltaX=distance, deltaY=0, moveClient=True)
                label = layout + '-drag-' + str(distance)
                ui = sample(label)
                position = latest['replay.timeline']['scrubber']['position']
                assert abs(position - destination) < 0.005, (label, position)
                send('capture.screenshot', path=str(session / (label + '.png')))
                knob_x = tx + inset + (tw - inset - 12) * position
                with Image.open(session / (label + '.png')) as image:
                    pixel = image.convert('RGB').getpixel((round(knob_x), round(ty + th / 2)))
                    assert min(pixel) > 220, (label, 'handle did not follow the drag', knob_x, pixel)
            if layout == 'Canvas':
                click(tx + tw + 30, ty + th / 2)
                ui = sample('details-open')
                assert ui['replayControlsBounds'][2] > 0, ui
                ui = hover(away_x, away_y, False, 'details-open-hidden')
                rx, ry, rw, rh = ui['replayControlsBounds']
                assert rw > 0 and rh > 0
                # The docked controls must remain drawn with transport hidden.
                with Image.open(session / 'details-open-hidden.png') as image:
                    pixel = image.convert('RGB').getpixel((int(rx + 12), int(ry + 10)))
                    assert max(pixel) > 25, pixel
                hover(tx + tw / 2, ty + th / 2, True, 'details-open-revealed')
                with Image.open(session / 'details-open-hidden.png') as hidden, Image.open(session / 'details-open-revealed.png') as shown:
                    crop = (int(rx), int(ry), int(rx + rw), int(ry + rh))
                    assert ImageChops.difference(hidden.convert('RGB').crop(crop), shown.convert('RGB').crop(crop)).getbbox() is None
                click(tx + tw + 30, ty + th / 2)
                ui = sample('details-closed')
        print('PASS: Editor/Canvas visibility, layout transitions, paused replay, captured drags and rendered handle positions')
    finally:
        try:
            send('session.stop')
        finally:
            connection.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    run(parser.parse_args().session)
