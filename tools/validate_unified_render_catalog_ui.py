"""Drive each Render/Sky/Cinematic catalog row and verify owner values."""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import re
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]
GAME = REPO / 'SkullbonezSource/Runtime/UI/GameUI'


def catalogs() -> dict[str, list[dict]]:
    source = (REPO / 'SkullbonezSource/Runtime/Render/UIRenderAuthoringCatalog.h').read_text()
    result = {}
    for name, enum in [('Render', 'UIRenderParam'), ('Cinematic', 'UICinematicParam')]:
        pattern = (r'\{ UIRenderAuthoringSection::(\w+), "([^"]+)", ' + enum +
                   r'::(\w+),\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*"([^"]+)"\s*\}')
        rows = re.findall(pattern, source)
        assert rows, name
        result[name] = [dict(section=section, label=label, param=param, minimum=float(lo),
                             maximum=float(hi), step=float(step), format=fmt, index=index)
                        for index, (section, label, param, lo, hi, step, fmt) in enumerate(rows)]
    canonical = {row['param']: row for row in result['Cinematic']}
    for name, table, base in [('Sky', 'kSkySliderSpecs', 146), ('Cinematic', 'kGameCinematicSliderSpecs', 266)]:
        source = (GAME / ('UITab' + name + '.cpp')).read_text()
        body = source.split(table + '[] = {', 1)[1].split('};', 1)[0]
        rows = re.findall(r'\{ (nullptr|"[^"]+"), "([^"]+)", SkullbonezCore::UI::UICinematicParam::(\w+) \}', body)
        y = base
        result[name] = []
        for section, label, param in rows:
            if section != 'nullptr':
                y += 28
            result[name].append(dict(canonical[param], label=label, rowY=y))
            y += 42
    y = 158
    previous = None
    for row in result['Render']:
        if row['section'] != previous:
            y += 28
        row['rowY'] = y
        previous = row['section']
        y += 42
    assert [len(result[key]) for key in ('Render', 'Sky', 'Cinematic')] == [38, 26, 64]
    return result


def run(session: Path, tabs: list[str], layouts: list[str]) -> None:
    rows_by_tab = catalogs()
    assert launch(session, REPO / 'Automation/SKULLBONEZ_CORE.exe',
                  REPO / 'SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json', hidden=True) == 0
    connection = SkarnessConnection(session)
    latest = {}
    offset = 0
    checks = []
    def send(command: str, **args: object) -> dict:
        result = connection.wait(connection.send(command, args))
        assert result.get('status') == 'applied', result
        return result
    def sample() -> dict:
        nonlocal offset
        send('run.step_frames', count=2)
        with (session / 'runtime.skarness.ndjson').open() as stream:
            stream.seek(offset)
            for line in stream:
                event = json.loads(line)
                if 'topic' in event:
                    latest[event['topic']] = event['payload']
            offset = stream.tell()
        return latest['ui.presentation']
    def click(x: float, y: float, hold: int = 0) -> None:
        send('input.pointer_drag', button='left', x=int(x), y=int(y), deltaX=0, deltaY=0, holdMilliseconds=hold)
    try:
        assert 'input.pointer_drag' in send('capabilities.get')['commands']
        send('state.subscribe', topics=[], detail='normal')
        ui = sample()
        for layout in layouts:
            if ui['layout'] != layout:
                click(ui['window'][0] - 110, 20)
                ui = sample()
            if not ui['toolsVisible']:
                click(ui['window'][0] - 38, 20)
                ui = sample()
            for tab in tabs:
                index = {'Render': 5, 'Sky': 8, 'Cinematic': 9}[tab]
                top = ui['viewport'][1] + ui['viewport'][3] + (28 if layout == 'Editor' else 0)
                click(14 + (ui['window'][0] - 28) * (index + .5) / 11, top + 66)
                ui = sample()
                assert ui['activeTool'] == index
                field = 'ordinaryRenderParameters' if tab == 'Render' else 'cinematicParameters'
                if tab != 'Render':
                    source = (GAME / ('UITab' + tab + '.cpp')).read_text()
                    table = 'kSkyFeatureSpecs' if tab == 'Sky' else 'kGameCinematicFeatureSpecs'
                    body = source.split(table + '[] = {', 1)[1].split('};', 1)[0]
                    features = re.findall(r'UICinematicFeature::(\w+)', body)
                    owner_order = ['Sky', 'Clouds', 'GodRays', 'VolumetricLight', 'Bloom', 'Fog', 'TerrainRelief', 'Shadows']
                    x, y, w, h = ui['toolsContentBounds']
                    for slot, feature in enumerate(features):
                        column_width = max(148, w * .46)
                        px = x + (column_width + 18 if slot % 2 else 0) + 30
                        row_y = (58 if tab == 'Sky' else 96) + 26 + (slot // 2) * 30
                        desired_scroll = max(0, row_y - h / 2)
                        delta = int(round((ui['toolsScroll'] - desired_scroll) * 120 / 42))
                        if delta:
                            send('input.pointer_wheel', x=int(x + 80), y=int(y + h / 2), wheelDelta=delta)
                            ui = sample()
                        py = y + row_y - ui['toolsScroll'] + 12
                        assert y <= py < y + h
                        before = list(ui['cinematicFeatures'])
                        click(px, py, hold=70)
                        ui = sample()
                        expected = list(before)
                        expected[owner_order.index(feature)] = not expected[owner_order.index(feature)]
                        assert ui['cinematicFeatures'] == expected, (layout, tab, feature, before, ui['cinematicFeatures'])
                        click(px, py)
                        ui = sample()
                        assert ui['cinematicFeatures'] == before
                        checks.append(dict(layout=layout, tab=tab, feature=feature, heldClick=True, restored=True))
                for row in rows_by_tab[tab]:
                    x, y, w, h = ui['toolsContentBounds']
                    desired_scroll = max(0, row['rowY'] - h / 2)
                    delta = int(round((ui['toolsScroll'] - desired_scroll) * 120 / 42))
                    if delta:
                        send('input.pointer_wheel', x=int(x + 80), y=int(y + h / 2), wheelDelta=delta)
                        ui = sample()
                    py = y + row['rowY'] - ui['toolsScroll'] + 17
                    assert y <= py < y + h, (layout, tab, row, ui)
                    for fraction in (0, 1, .37):
                        px = int(x + 1 if fraction == 0 else x + w - 1 if fraction == 1 else x + 118 + (w - 190) * fraction)
                        actual_fraction = min(1, max(0, (px - x - 118) / max(80, w - 190)))
                        expected = row['minimum'] + (row['maximum'] - row['minimum']) * actual_fraction
                        if row['step'] > 0:
                            expected = row['minimum'] + math.floor((expected - row['minimum']) / row['step'] + .5) * row['step']
                        expected = min(row['maximum'], max(row['minimum'], expected))
                        click(px, py)
                        ui = sample()
                        actual = ui[field][row['index']]
                        assert math.isclose(actual, expected, rel_tol=2e-5, abs_tol=max(1e-6, row['step'] * .01)), (layout, tab, row, fraction, expected, actual)
                        checks.append(dict(layout=layout, tab=tab, parameter=row['param'], fraction=fraction, expected=expected, actual=actual))
                    print(f"PASS {layout}/{tab}/{row['param']}", flush=True)
                send('capture.screenshot', path=str((session / f'{layout}-{tab}-last-row.png').resolve()))
                before = list(ui[field])
                click(ui['window'][0] - 38, 20)
                ui = sample()
                assert not ui['toolsVisible']
                click(px, py)
                ui = sample()
                assert ui[field] == before, (layout, tab, 'hidden control changed owner')
                click(ui['window'][0] - 38, 20)
                ui = sample()
        print(f'PASS: {len(checks)} native parameter endpoint/interior checks', flush=True)
    finally:
        (session / 'catalog-checks.json').write_text(json.dumps(checks, indent=2))
        try:
            send('session.stop')
        finally:
            connection.close()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, default=REPO / 'TestOutput/skarness/unified-render-catalog')
    parser.add_argument('--tabs', nargs='+', choices=['Render', 'Sky', 'Cinematic'], default=['Render', 'Sky', 'Cinematic'])
    parser.add_argument('--layouts', nargs='+', choices=['Canvas', 'Editor'], default=['Canvas', 'Editor'])
    args = parser.parse_args()
    run(args.session, args.tabs, args.layouts)
