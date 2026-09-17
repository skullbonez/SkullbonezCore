"""Click Causes category filters and verify the rendered projection and selection."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from skarness import SkarnessConnection, launch
from validate_catto_causal_playback import StateReader
from validate_skarness_causal_playback import show_cause_surface

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    assert launch(session, REPO / 'Automation/SKULLBONEZ_CORE.exe',
                  REPO / 'SkullbonezData/scenes/catto_double_domino.scene.json', hidden=True) == 0
    connection = SkarnessConnection(session)
    reader = StateReader(session / 'runtime.skarness.ndjson')
    results = []

    def send(command: str, **arguments) -> dict:
        result = connection.wait(connection.send(command, arguments))
        assert result.get('status') == 'applied', result
        return result

    def state() -> dict:
        send('run.step_frames', count=2)
        return reader.read()

    try:
        capabilities = send('capabilities.get')
        assert {'input.pointer_drag', 'replay.set_cause_filter_text'} <= {item['name'] for item in capabilities['catalog']}
        send('state.subscribe', topics=[], detail='normal')
        send('prediction.select_target', name='domino_01')
        send('replay.set_prediction_detail', highDetail=True)
        send('replay.set_prediction_enabled', enabled=True)
        send('run.until', condition='prediction.complete', maxFrames=10000)
        show_cause_surface(connection, session)
        rows = state()['replay.cause']['rows']
        selected_row = next(i for i, row in enumerate(rows) if row['kind'] == 2)
        selected = rows[selected_row]
        send('replay.select_cause_row', row=selected_row)
        send('run.until', condition='camera.inspection_settled', maxFrames=1000)
        baseline = state()['replay.state']
        assert baseline['publishedPredictionTargetId'] == baseline['submittedPredictionTargetId'] == 1036
        for query in ('', 'domino_', 'solver row', 'no_such_evidence'):
            send('replay.set_cause_filter_text', text=query)
            seen = {}
            for index in (0, 1, 2, 0):
                cause = state()['replay.cause']
                x, y, width, height = cause['filterBounds'][index]
                send('input.pointer_drag', button='left', x=int(x + width / 2), y=int(y + height / 2), deltaX=0, deltaY=0)
                topics = state()
                cause, replay = topics['replay.cause'], topics['replay.state']
                assert cause['filter'] == index and cause['filterText'] == query, cause
                assert cause['selectedRow'] == selected_row
                assert replay['selectedCausePrimaryId'] == selected['id']
                assert replay['selectedCauseCounterpartId'] == selected['counterpartId']
                for key in ('predictionGeneration', 'causeTargetFrame', 'causePresentedFrame', 'inspectionCameraFocusKind'):
                    assert replay[key] == baseline[key], (key, baseline[key], replay[key])
                visible = cause['visibleRows']
                assert len(visible) == cause['visibleRowCount']
                kinds = {rows[row]['kind'] for row in visible}
                if index == 1:
                    assert kinds <= {0, 4}, kinds
                elif index == 2:
                    assert kinds <= {0, 1, 3}, kinds
                if query in ('solver row', 'no_such_evidence') and index != 0:
                    assert not visible, visible
                if query == 'no_such_evidence':
                    assert not visible, visible
                if index in seen:
                    assert visible == seen[index], 'All did not restore the same source row identities'
                seen[index] = visible
                label = f'{query or "empty"}-{index}'.replace(' ', '-')
                (session / f'{label}.json').write_text(json.dumps(cause, indent=2), encoding='utf-8')
                if not query:
                    send('capture.screenshot', path=str(session / f'{label}.png'))
                results.append({'query': query, 'filter': index, 'visible': len(visible), 'passed': True})
            if not query:
                assert len(seen[1]) < len(seen[2]) < len(seen[0])
                assert selected_row in seen[0] and selected_row not in seen[1] and selected_row not in seen[2]
                assert len(seen[0]) == len(rows)
        (session / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
        print(json.dumps(results, indent=2))
    finally:
        send('session.stop')
        connection.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    run(parser.parse_args().session.resolve())
