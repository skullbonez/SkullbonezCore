"""Verify Space 200 horizon drags preserve visible curves at every observed frame."""
from __future__ import annotations
import argparse
import json
import time
from pathlib import Path
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    session = session.resolve()
    assert launch(session, REPO / 'Automation/SKULLBONEZ_CORE.exe',
                  REPO / 'SkullbonezData/scenes/space_field_200.scene.json',
                  hidden=True, worker_threads=4, allocation_guard='gameplay') == 0
    connection = SkarnessConnection(session)
    latest = {}
    offset = 0
    observations = []
    continuity = False
    first_shape = None
    generation = None
    last_count = 0

    def send(command, **arguments):
        result = connection.wait(connection.send(command, arguments))
        assert result.get('status') == 'applied', (command, result)
        return result

    def sample():
        nonlocal offset, last_count
        send('run.step_frames', count=2)
        with (session / 'runtime.skarness.ndjson').open('rb') as stream:
            stream.seek(offset)
            for line in stream:
                if not line.endswith(b'\n'):
                    break
                offset += len(line)
                event = json.loads(line)
                if 'topic' not in event:
                    continue
                latest[event['topic']] = event['payload']
                if continuity and event['topic'] == 'replay.visual_packet':
                    packet = event['payload']
                    path = packet['activePath']
                    row = {'frame': event.get('renderFrame'), 'count': path['ordinaryRecords'],
                           'firstCount': path['firstSegmentCount'], 'firstHash': path['firstSegmentHash']}
                    observations.append(row)
                    assert packet['header']['targetId'] == 1, row
                    assert (row['firstCount'], row['firstHash']) == first_shape, ('curve changed', row, first_shape)
                    assert row['count'] >= last_count, ('visible paths disappeared', last_count, row)
                    last_count = row['count']
        return latest['ui.presentation']

    def center(bounds):
        x, y, w, h = bounds
        send('input.pointer_position', enabled=True, x=round(x+w/2), y=round(y+h/2))
        sample()
        send('input.pointer_drag', button='left', x=round(x+w/2), y=round(y+h/2), deltaX=0, deltaY=0)
        return sample()

    def horizon(seconds):
        ui = sample()
        x, y, w, _ = ui['replayControlsBounds']
        old = latest['replay.prediction.controls']['horizonSeconds']
        start = round(x+18+(w-74)*(old-1)/119)
        end = round(x+18+(w-74)*(seconds-1)/119)
        send('input.pointer_position', enabled=True, x=start, y=round(y+147))
        sample()
        send('input.pointer_drag', button='left', x=start, y=round(y+147), deltaX=end-start, deltaY=0,
             moveClient=True, holdAfterMoveMilliseconds=60)
        sample()
        actual = latest['replay.prediction.controls']['horizonSeconds']
        assert abs(actual-seconds) <= 1, (actual, seconds)
        assert latest['replay.state']['predictionGeneration'] == generation
        return actual

    def ready(seconds, target=1):
        deadline = time.monotonic()+180
        while time.monotonic() < deadline:
            sample()
            state = latest['replay.state']
            packet = latest['replay.visual_packet']
            path = packet['activePath']
            roots = [record for record in latest['replay.prediction.trajectories']['records']
                     if record['lane'] == 1 and record['branchOrdinal'] == 0]
            published = (len(roots) == 200
                         and all(record['publishedPointCount'] >= round(seconds*120)+1 for record in roots))
            if (published and packet['header']['predictionComplete']
                    and packet['header']['publishedFrameCount'] == round(seconds*120)+1
                    and state['predictionComplete'] and not state['predictionBuilding']
                    and state['publishedPredictionFrames'] == round(seconds*120)+1
                    and state['pathTargetId'] == state['publishedPredictionTargetId'] == state['submittedPredictionTargetId'] == target
                    and path['firstSegmentCount'] == 200):
                return
        raise AssertionError(latest['replay.state'])

    def capture(label):
        send('input.pointer_position', enabled=True, x=1100, y=700)
        sample()
        (session / (label+'.json')).write_text(json.dumps(latest, indent=2), encoding='utf-8')
        send('capture.screenshot', path=str(session / (label+'.png')))

    try:
        commands = set(send('capabilities.get')['commands'])
        assert {'input.pointer_drag', 'prediction.select_target', 'state.subscribe'} <= commands
        send('state.subscribe', topics=[], detail='normal')
        send('input.set_focus', focused=True)
        send('replay.set_prediction_horizon', seconds=5)
        send('prediction.select_target', name='field_000')
        send('replay.set_prediction_enabled', enabled=True)
        ui = sample()
        if ui['layout'] != 'Editor':
            ui = center(ui['headerLayoutBounds'])
        if ui['replayControlsBounds'][2] == 0:
            center(ui['editorReplayTabBounds'])
        ready(5)
        capture('initial5')
        generation = latest['replay.state']['predictionGeneration']
        path = latest['replay.visual_packet']['activePath']
        first_shape = (path['firstSegmentCount'], path['firstSegmentHash'])
        last_count = path['ordinaryRecords']
        continuity = True
        for seconds in [6,7,8,9,10,12,15,20,30,40,60,90,120]:
            actual = horizon(seconds)
            print('drag', actual, 'records', last_count, flush=True)
        ready(actual)
        capture('extended120')
        full_count = last_count
        continuity = False
        shorter = horizon(5)
        ready(shorter)
        capture('trimmed5')
        assert latest['replay.visual_packet']['header']['revealFrame'] <= round(shorter*120)
        assert latest['replay.visual_packet']['activePath']['ordinaryRecords'] < full_count
        path = latest['replay.visual_packet']['activePath']
        assert (path['firstSegmentCount'],path['firstSegmentHash']) == first_shape
        last_count = path['ordinaryRecords']
        continuity = True
        actual = horizon(120)
        ready(actual)
        capture('cached120')
        assert last_count == full_count
        continuity = False
        shorter = horizon(20)
        send('prediction.select_target', name='field_010')
        ready(shorter, 11)
        capture('new-target')
        result = {'passed': True, 'observedFrames': len(observations), 'fullRecords': full_count,
                  'generation': generation, 'observations': observations}
        (session / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print('PASS: Space 200 curves stayed visible and retained their first segments across horizon drags', flush=True)
    finally:
        (session / 'observations.json').write_text(json.dumps(observations, indent=2), encoding='utf-8')
        try:
            send('session.stop')
        finally:
            connection.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    run(parser.parse_args().session)
