"""Check domino causal ancestry, detail-row time transport, and single camera tweens."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from skarness import SkarnessConnection, launch
from validate_skarness_causal_playback import show_cause_surface

REPO = Path(__file__).resolve().parents[1]


def check_chain(rows: list[dict]) -> list[dict]:
    bodies = {row['id']: row for row in rows if row['kind'] == 0}
    chain = [bodies[1035 + index] for index in range(15)]
    for index, body in enumerate(chain):
        assert body['name'] == f'domino_{index:02}', body
        if index:
            assert body['parentId'] == chain[index - 1]['id'], body
            assert body['firstFrame'] > chain[index - 1]['firstFrame'], body
    return chain


def check_camera(samples: list[dict]) -> None:
    progress = [sample['camera']['tweenProgress'] for sample in samples]
    assert all(b >= a - 0.00001 for a, b in zip(progress, progress[1:])), progress
    assert not samples[-1]['camera']['tweenActive'], samples[-1]
    assert samples[-1]['replay']['causeInspectionMode'] == 2, samples[-1]


class StateReader:
    def __init__(self, path: Path):
        self.path = path
        self.offset = 0
        self.topics: dict = {}

    def read(self) -> dict:
        with self.path.open(encoding='utf-8') as stream:
            stream.seek(self.offset)
            while True:
                offset = stream.tell()
                line = stream.readline()
                if not line or not line.endswith('\n'):
                    stream.seek(offset)
                    break
                event = json.loads(line)
                if 'topic' in event:
                    self.topics[event['topic']] = event['payload']
            self.offset = stream.tell()
        return self.topics


def run(session: Path, executable: Path) -> None:
    scene = REPO / 'SkullbonezData/scenes/catto_double_domino.scene.json'
    assert launch(session, executable, scene, hidden=True) == 0
    connection = SkarnessConnection(session)
    reader = StateReader(session / 'runtime.skarness.ndjson')
    results = []

    def send(command: str, **arguments) -> dict:
        result = connection.wait(connection.send(command, arguments))
        assert result.get('status') == 'applied', result
        return result

    def state() -> dict:
        send('run.step_frames', count=1)
        return reader.read()

    try:
        capabilities = send('capabilities.get')
        names = {row['name'] for row in capabilities['catalog']}
        assert {'replay.select_cause_row', 'input.pointer_drag', 'input.set_arrows'} <= names
        send('state.subscribe', topics=[], detail='normal')
        send('replay.set_prediction_detail', highDetail=True)
        send('replay.set_prediction_horizon', seconds=20.0)
        send('prediction.select_target', name='domino_00')
        send('replay.set_prediction_enabled', enabled=True)
        send('run.until', condition='prediction.complete', maxFrames=10000)
        ready = state()
        rows = ready['replay.cause']['rows']
        chain = check_chain(rows)
        (session / 'chain.json').write_text(json.dumps(chain, indent=2), encoding='utf-8')
        generation = ready['replay.state']['predictionGeneration']
        show_cause_surface(connection, session)

        # Adjacent detail rows share time but change the camera endpoint. Keeping
        # each pair consecutive catches the fallback-clock restart after landing.
        for index, kind in ((2, 1), (2, 2), (12, 1), (12, 2), (14, 1), (14, 2)):
            body = chain[index]
            row_index = next(i for i, row in enumerate(rows)
                             if row['id'] == body['id'] and row['kind'] == kind
                             and row['counterpartId'] == chain[index - 1]['id'])
            send('replay.select_cause_row', row=row_index)
            samples = []
            for _ in range(90):
                topics = state()
                samples.append({'replay': topics['replay.state'], 'camera': topics['camera.state']})
            label = f'domino-{index:02}-kind-{kind}'
            (session / f'{label}.json').write_text(json.dumps(samples, indent=2), encoding='utf-8')
            check_camera(samples)
            selected = samples[-1]['replay']
            assert selected['causePresentedFrame'] == body['firstFrame'], selected
            assert selected['selectedCausePrimaryId'] == body['id'], selected
            assert selected['selectedCauseCounterpartId'] == chain[index - 1]['id'], selected
            assert selected['publishedPredictionTargetId'] == selected['submittedPredictionTargetId'] == 1035
            assert selected['predictionGeneration'] == generation
            if kind == 2:
                send('input.set_arrows', left=True, right=False)
                send('run.step_frames', count=12)
                reversed_state = state()['replay.state']
                send('input.set_arrows', left=False, right=False)
                assert reversed_state['causePresentedFrame'] < body['firstFrame'], reversed_state
                x, y, width, height = reader.read()['ui.presentation']['transportBounds']
                inset = min(72, width * 0.22)
                track_x, track_width = x + inset, max(1, width - inset - 12)
                send('input.pointer_drag', button='left', x=int(track_x + track_width * 0.45),
                     y=int(y + height / 2), deltaX=int(track_width * 0.25), deltaY=0, moveClient=True)
                dragged = state()['replay.state']
                assert dragged['causePresentedFrame'] != reversed_state['causePresentedFrame'], dragged
                assert dragged['selectedCauseRow'] == row_index
                assert dragged['selectedCausePrimaryId'] == body['id']
                assert dragged['causeTargetFrame'] == body['firstFrame']
                send('capture.screenshot', path=str((session / f'{label}.png').resolve()))
            results.append({'name': body['name'], 'kind': kind, 'frame': body['firstFrame'], 'passed': True})
            print(f"PASS: {label} frame {body['firstFrame']}", flush=True)
        (session / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    finally:
        send('session.stop')
        connection.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=REPO / 'Automation/SKULLBONEZ_CORE.exe')
    args = parser.parse_args()
    run(args.session.resolve(), args.exe.resolve())


if __name__ == '__main__':
    main()
