"""Exercise eased panel transitions through native pointer routes and a pinned presentation clock."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from skarness import SkarnessConnection, launch
from validate_ui_themes import wait_for_exit

REPO = Path(__file__).resolve().parents[1]

def run(directory: Path, theme: int = 0) -> None:
    directory = directory.resolve()
    prefs = directory.parent / (directory.name + '.preferences')
    prefs.parent.mkdir(parents=True, exist_ok=True)
    prefs.write_text('version 2\nlayout 1\nleft 280\nright 360\ndrawer 480\ndiagnostics 140\nfolded 0\ntool 4\nleftFolded 0\nrightFolded 0\ntheme %d\n' % theme)
    assert launch(directory, REPO/'Automation/SKULLBONEZ_CORE.exe',
                  REPO/'SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json',
                  hidden=True, layout_file=prefs) == 0
    connection = SkarnessConnection(directory)
    offset = 0
    latest = {}
    now = 0.0
    def send(command, **args):
        result = connection.wait(connection.send(command, args))
        assert result.get('status') == 'applied', result
        return result
    def sample(label):
        nonlocal offset
        send('run.step_frames', count=3)
        with (directory/'runtime.skarness.ndjson').open() as stream:
            stream.seek(offset)
            for line in stream:
                row = json.loads(line)
                if 'topic' in row: latest[row['topic']] = row['payload']
            offset = stream.tell()
        ui = latest['ui.presentation']
        (directory/(label+'.json')).write_text(json.dumps(latest, indent=2))
        assert not ui['panelDrawOverflow'], ui
        return ui
    def clock(seconds):
        nonlocal now
        now = seconds
        send('ui.animation_clock', seconds=now, enabled=True)
    def capture(label):
        send('input.pointer_position', x=800, y=180, enabled=True)
        send('capture.screenshot', path=str(directory/(label+'.png')))
    def click(bounds):
        x,y,w,h = bounds
        send('input.pointer_drag', button='left', x=int(x+w/2), y=int(y+h/2), deltaX=0, deltaY=0)
    def key(code):
        send('input.set_key', key=code, down=True)
        send('run.step_frames', count=2)
        send('input.set_key', key=code, down=False)
    def expect(ui, panel, value):
        assert abs(ui['panelVisibility'][panel]-value) < .002, (panel,value,ui['panelVisibility'])
    try:
        assert 'ui.animation_clock' in send('capabilities.get')['commands']
        send('state.subscribe', topics=[], detail='normal')
        clock(0)
        sample('start')
        clock(1)
        ui = sample('settled')
        capture('scene-settled')
        # Header menus are separate foreground panels, even while the header stays open.
        clock(2)
        x,y,w,h=ui['cameraPopupBounds']
        camera_button=[x,6,w,24]
        click(camera_button)
        ui=sample('camera-popup-start')
        assert ui['cameraPopupOpen']
        clock(2.08)
        ui=sample('camera-popup-half')
        expect(ui,12,.875)
        capture('camera-popup-half')
        click(camera_button)
        ui=sample('camera-popup-close')
        assert not ui['cameraPopupOpen']
        clock(2.3)
        ui=sample('camera-popup-closed')
        expect(ui,12,0)
        for panel,bounds in ((1,'leftFoldBounds'),(2,'rightFoldBounds')):
            start = now + 1
            clock(start)
            click(ui[bounds])
            expect(sample('fold-start-'+str(panel)),panel,1)
            clock(start+.08)
            ui = sample('fold-half-'+str(panel))
            expect(ui,panel,.875)
            capture('fold-half-'+str(panel))
            # Reverse halfway. The same panel resumes from its drawn position.
            click(ui[bounds])
            expect(sample('reverse-'+str(panel)),panel,.875)
            clock(start+.2)
            ui = sample('reopened-'+str(panel))
            expect(ui,panel,1)
        start = now+1
        clock(start)
        click(ui['replayDetailsBounds'])
        ui = sample('tools-enter-start')
        assert ui['toolsVisible']
        expect(ui,4,0)
        for delta,value in ((.04,.578125),(.08,.875),(.2,1)):
            clock(start+delta)
            ui = sample('tools-enter-'+str(delta))
            expect(ui,4,value)
            capture('tools-enter-'+str(delta))
        start = now+1
        clock(start)
        click(ui['replayDetailsBounds'])
        ui = sample('tools-exit-start')
        assert not ui['toolsVisible']
        clock(start+.08)
        expect(sample('tools-exit-half'),4,.875)
        capture('tools-exit-half')
        clock(start+.2)
        ui=sample('tools-closed')
        expect(ui,4,0)
        # Both diagnostics use the same easing, and Tools retires both at once.
        start=now+1
        clock(start)
        key(0x74)
        key(0x75)
        sample('diagnostics-start')
        clock(start+.08)
        ui=sample('diagnostics-half')
        expect(ui,5,.875)
        expect(ui,6,.875)
        capture('diagnostics-half')
        clock(start+.2)
        ui=sample('diagnostics-open')
        clock(now+1)
        start=now
        click(ui['replayDetailsBounds'])
        sample('tools-retires-diagnostics')
        clock(start+.08)
        ui=sample('diagnostics-retiring')
        expect(ui,5,.875)
        expect(ui,6,.875)
        expect(ui,4,.875)
        capture('tools-and-diagnostics')
        clock(start+.2)
        ui=sample('tools-replaced-diagnostics')
        expect(ui,5,0)
        expect(ui,6,0)
        # Texture previews must blend with the same opacity as their frame and labels.
        click([14+(ui['window'][0]-28)*6/11, ui['drawerBounds'][1]+54,
               (ui['window'][0]-28)/11, 24])
        ui=sample('targets-open')
        assert ui['activeTool']==6
        capture('targets-open')
        key(0x74)
        key(0x75)
        ui=sample('diagnostics-reopened-over-tools')
        assert ui['markerHistoryVisible'] and ui['memoryWaterlineVisible']
        clock(now+1)
        sample('diagnostics-restored-settled')
        key(0x74)
        key(0x75)
        sample('diagnostics-close-again')
        clock(now+1)
        ui=sample('targets-ready-to-close')
        start=now
        click(ui['replayDetailsBounds'])
        sample('tools-close-before-lab')
        clock(start+.08)
        expect(sample('preview-exit-half'),4,.875)
        capture('preview-exit-half')
        clock(now+1)
        sample('before-lab')
        send('comparison.load', path=str(REPO/'SkullbonezData/solver-lab/wall-only/comparison.json'))
        ui=sample('lab')
        assert ui['workspace']=='Solver Lab'
        clock(now+1)
        ui=sample('lab-settled')
        capture('lab-settled')
        for panel,bounds in ((1,'leftFoldBounds'),(2,'rightFoldBounds')):
            clock(now+1)
            start=now
            click(ui[bounds])
            sample('lab-fold-start-'+str(panel))
            clock(start+.08)
            ui=sample('lab-fold-half-'+str(panel))
            expect(ui,panel,.875)
            capture('lab-fold-half-'+str(panel))
            clock(start+.2)
            ui=sample('lab-fold-closed-'+str(panel))
            expect(ui,panel,0)
            click(ui[bounds])
            sample('lab-open-start-'+str(panel))
            clock(start+.28)
            ui=sample('lab-open-half-'+str(panel))
            expect(ui,panel,.875)
            capture('lab-open-half-'+str(panel))
            clock(start+.5)
            ui=sample('lab-reopened-'+str(panel))
        # Returning to real time must not strand a partly opened panel.
        send('ui.animation_clock', seconds=0, enabled=False)
        sample('real-clock-restored')
        print('PASS: Scene/Solver Lab, Tools, F5/F6, 160 ms easing, reversal, retained exits and bounded draw commands')
    finally:
        try: send('session.stop')
        finally: connection.close()
        wait_for_exit(directory)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    parser.add_argument('--theme', type=int, choices=(0,1,2), default=0)
    args=parser.parse_args()
    run(args.session, args.theme)
