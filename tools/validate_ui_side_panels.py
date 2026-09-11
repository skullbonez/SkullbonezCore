"""Check independent side drawers, native edge drags and floating diagnostics."""
from __future__ import annotations
import argparse
import json
import time
from pathlib import Path
from skarness import SkarnessConnection, launch
from validate_ui_themes import wait_for_exit

REPO = Path(__file__).resolve().parents[1]

def run(directory: Path, theme: int) -> None:
    directory = directory.resolve()
    prefs = directory.parent / (directory.name + '.preferences')
    prefs.parent.mkdir(parents=True, exist_ok=True)
    prefs.write_text('version 3\nlayout 1\nleft 280\nright 360\ndrawer 400\ndiagnostics 140\nfolded 7\ntool 4\nleftFolded 1\nrightFolded 1\ntheme %d\nreplayFolded 1\n' % theme)
    assert launch(directory, REPO/'Automation/SKULLBONEZ_CORE.exe',
                  REPO/'SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json',
                  hidden=True, layout_file=prefs) == 0
    connection = SkarnessConnection(directory)
    latest = {}
    offset = 0
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
        (directory/(label+'.json')).write_text(json.dumps(latest, indent=2))
        ui = latest['ui.presentation']
        assert not ui['panelDrawOverflow'], ui
        return ui
    def clock(delta=1):
        nonlocal now
        now += delta
        send('ui.animation_clock', seconds=now, enabled=True)
    def click(bounds, dx=0):
        x,y,w,h = bounds
        assert w > 0 and h > 0, bounds
        send('input.pointer_drag', button='left', x=int(x+w/2), y=int(y+h/2), deltaX=dx, deltaY=0, moveClient=True)
    def capture(label):
        send('input.pointer_position', x=800, y=180, enabled=True)
        send('capture.screenshot', path=str(directory/(label+'.png')))
    def settled(label):
        clock()
        return sample(label)
    def expect(ui, panel, opacity):
        assert abs(ui['panelVisibility'][panel] - opacity) < .002, (panel, opacity, ui['panelVisibility'])
    def key(code):
        send('input.set_key', key=code, down=True)
        send('run.step_frames', count=2)
        send('input.set_key', key=code, down=False)
    def contained(bounds, region):
        x,y,w,h = bounds; rx,ry,rw,rh = region
        assert w > 0 and h > 0 and x >= rx and y >= ry and x+w <= rx+rw+.1 and y+h <= ry+rh+.1, (bounds, region)
    def diagnostics(ui):
        for name in ('markerHistoryBounds', 'memoryWaterlineBounds'):
            contained(ui[name],[0,0,*ui['window']])
    try:
        commands=send('capabilities.get')['commands']
        assert {'ui.animation_clock','input.pointer_drag','replay.set_prediction_detail','comparison.state'} <= set(commands)
        send('state.subscribe', topics=[], detail='normal')
        clock(); sample('initial'); ui=settled('folded')
        capture('theme-rails')
        click(ui['editorTabBounds']); ui=sample('editor-enter-start'); expect(ui,1,0)
        clock(.08); ui=sample('editor-enter-half'); expect(ui,1,.875); capture('editor-enter-half')
        ui=settled('editor-open'); editor=ui['editorControlsBounds']
        click(ui['editorReplayTabBounds']); ui=sample('replay-enter-start'); expect(ui,13,0)
        clock(.08); ui=sample('replay-enter-half'); expect(ui,13,.875); capture('replay-enter-half')
        ui=settled('both-open')
        assert ui['editorControlsBounds'][1]==editor[1]
        assert ui['editorControlsBounds'][3]<editor[3]
        assert ui['replayControlsBounds'][1] == ui['editorControlsBounds'][1]+ui['editorControlsBounds'][3]+30
        capture('stacked-sections')
        for name,dx in (('leftResizeBounds',120),('replayResizeBounds',80),('rightResizeBounds',-120)):
            if name=='rightResizeBounds':
                click(ui['causeTabBounds']); sample('causes-start'); clock(.08)
                ui=sample('causes-half'); expect(ui,2,.875); capture('causes-half'); ui=settled('causes-open')
            before=ui[name]
            click(before,dx); ui=sample('drag-'+name)
            assert abs(ui[name][0]-before[0]-dx)<2, (before,ui[name])
        ui=settled('resized'); capture('resized-sections')
        click(ui['leftFoldBounds']); sample('editor-close'); ui=settled('only-replay')
        assert ui['editorControlsBounds'][2]==0 and ui['replayControlsBounds'][2]>0
        click(ui['replayFoldBounds']); sample('replay-close'); ui=settled('both-closed')
        assert ui['editorControlsBounds'][2]==0 and ui['replayControlsBounds'][2]==0
        key(0x74); key(0x75); sample('diagnostics-start'); ui=settled('diagnostics-over-scene'); diagnostics(ui); capture('diagnostics-over-scene')
        for name in ('markerHistoryBounds','memoryWaterlineBounds'):
            original=ui[name]
            assert original[2:]==[340,166],original
            x,y,w,h=original
            send('input.pointer_drag',button='left',x=int(x+75),y=int(y+12),deltaX=150,deltaY=45,moveClient=True)
            ui=sample('move-'+name)
            assert abs(ui[name][0]-x-150)<2 and abs(ui[name][1]-y-45)<2,(name,original,ui[name])
            x,y,w,h=ui[name]
            send('input.pointer_drag',button='left',x=int(x+w-3),y=int(y+h-3),deltaX=45,deltaY=30,moveClient=True)
            ui=sample('resize-'+name)
            assert abs(ui[name][2]-w-45)<2 and abs(ui[name][3]-h-30)<2,(name,ui[name])
        capture('floating-diagnostics-moved')
        x,y,w,h=ui['markerHistoryBounds']; memory=ui['memoryWaterlineBounds'][:]
        dy=int(memory[1]-y)
        send('input.pointer_drag',button='left',x=int(x+75),y=int(y+12),deltaX=0,deltaY=dy,moveClient=True)
        ui=sample('histogram-drag-crosses-memory')
        assert abs(ui['markerHistoryBounds'][1]-y-dy)<2 and ui['memoryWaterlineBounds']==memory
        # Move F6 aside to expose F5 again; both overlays retain independent bounds.
        send('input.pointer_drag',button='left',x=int(memory[0]+75),y=int(memory[1]+12),deltaX=420,deltaY=0,moveClient=True)
        ui=sample('memory-drag-after-overlap')
        assert abs(ui['memoryWaterlineBounds'][0]-memory[0]-420)<2
        click(ui['replayDetailsBounds']); ui=sample('tools-start')
        assert ui['toolsVisible'] and ui['markerHistoryVisible'] and ui['memoryWaterlineVisible']
        ui=settled('tools-open'); sample('diagnostics-over-tools-start')
        ui=settled('diagnostics-above-tools'); diagnostics(ui); capture('diagnostics-above-tools')
        key(0x50); ui=settled('p-keeps-tools'); assert ui['toolsVisible']; capture('p-keeps-tools')
        assert latest['replay.prediction.controls']['enabled']
        assert ui['markerHistoryVisible'] and ui['memoryWaterlineVisible']
        key(0x50); ui=settled('p-exit-keeps-tools'); assert ui['toolsVisible']
        assert not latest['replay.prediction.controls']['enabled']
        x,y,w,h=ui['drawerBounds']; click([x+w-40,y+8,30,28]); ui=sample('tools-close'); assert not ui['toolsVisible']
        key(0x74); key(0x75); ui=settled('tools-closed')
        send('replay.set_prediction_detail',highDetail=True)
        send('replay.set_prediction_horizon',seconds=7.5)
        send('prediction.select_target',name='path_striker')
        send('replay.set_prediction_enabled',enabled=True)
        send('run.until',condition='prediction.complete',maxFrames=3000)
        ui=settled('prediction-ready')
        cause=latest['replay.cause']; target=latest['selection.state']['pathTargetId']
        assert target and latest['replay.prediction.controls']['sourceTargetId']==target and cause['rowCount']>2
        send('replay.select_cause_row',row=2); send('run.until',condition='camera.inspection_settled',maxFrames=1000)
        ui=settled('cause-selected'); selected=latest['selection.state']['selectedCausePrimaryId']
        x,y,w,h=ui['causeControlsBounds']; original_window=latest['replay.cause']['window']
        click([x+w-88,y+6,80,26]); ui=sample('evidence-start'); expect(ui,14,0)
        clock(.08); ui=sample('evidence-half'); expect(ui,14,.875); capture('evidence-half')
        ui=settled('evidence-open'); capture('evidence-open')
        assert latest['replay.cause']['summaryExpandedSections']==0
        assert latest['replay.cause']['drawerOpen'] and latest['replay.cause']['window']==original_window
        assert latest['selection.state']['selectedCausePrimaryId']==selected
        ew=min(x-ui['viewport'][0],max(400,w)); ex=x-ew; top=ui['viewport'][1]
        for tab in (1,2,0):
            click([ex+12+(ew-24)*tab/3,top+88,(ew-24)/3,28]); sample('evidence-tab-'+str(tab)); capture('evidence-tab-'+str(tab))
            assert latest['replay.cause']['activeTab']==tab
            assert latest['selection.state']['selectedCausePrimaryId']==selected
        # The inspector has its own close, while the hierarchy stays available.
        click([x-32,top+6,26,26]); ui=sample('evidence-close'); assert not latest['replay.cause']['drawerOpen']
        ui=settled('evidence-closed'); expect(ui,14,0)
        click(ui['rightFoldBounds']); sample('causes-close'); ui=settled('causes-closed'); expect(ui,2,0)
        click(ui['causeTabBounds']); ui=sample('cause-reenter'); expect(ui,2,0)
        clock(.08); ui=sample('cause-reenter-half'); expect(ui,2,.875); capture('cause-reenter-half'); ui=settled('cause-reentered')
        click(ui['headerWorkspaceBounds'])
        deadline=time.monotonic()+90
        while True:
            ui=sample('lab-loading'); comparison=send('comparison.state')['result']['comparison']
            assert not comparison['loadError'],comparison
            if comparison['active'] and not comparison['loading']: break
            assert time.monotonic()<deadline,comparison
        ui=settled('lab-open')
        click(ui['editorTabBounds']); ui=sample('lab-controls-start'); expect(ui,1,0)
        clock(.08); ui=sample('lab-controls-half'); expect(ui,1,.875); capture('lab-controls-half'); ui=settled('lab-controls-open')
        before=ui['leftResizeBounds']; click(before,60); ui=sample('lab-resize'); assert abs(ui['leftResizeBounds'][0]-before[0]-60)<2
        key(0x74); key(0x75); ui=settled('lab-diagnostics'); diagnostics(ui); capture('lab-diagnostics')
        send('window.resize',width=900,height=640); ui=settled('small-lab'); diagnostics(ui); capture('small-lab')
        print('PASS: independent sections, edge drags, top-down easing, attached evidence, theme rails and floating, draggable F5/F6; theme',theme)
    finally:
        try: send('session.stop')
        finally: connection.close(); wait_for_exit(directory)

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--session',type=Path,required=True); parser.add_argument('--theme',type=int,default=0)
    args=parser.parse_args(); run(args.session,args.theme)
