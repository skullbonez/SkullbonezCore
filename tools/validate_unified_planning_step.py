"""Compare an uncommitted-plan step against cancellation before the same step."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from skarness import SkarnessConnection, launch

REPO=Path(__file__).resolve().parents[1]

def scenario(session: Path, converged: bool, cancel_first: bool) -> dict:
    session.mkdir(parents=True,exist_ok=True)
    data=json.loads((REPO/'SkullbonezData/scenes/solar_system.scene.json').read_text())
    mars=next(obj for obj in data['objects'] if obj['name']=='mars')
    ship=next(obj for obj in data['objects'] if obj['name']=='ship')
    ship['position']=[mars['position'][0]+3.1,*mars['position'][1:]]
    ship['velocity']=mars['velocity'][:]
    scene=session/'near-intercept.scene.json'
    scene.write_text(json.dumps(data,indent=2))
    assert launch(session,REPO/'Automation/SKULLBONEZ_CORE.exe',scene,hidden=True)==0
    c=SkarnessConnection(session); latest={}; offset=0
    def send(command,**args):
        result=c.wait(c.send(command,args)); assert result.get('status')=='applied',result
        return result
    def sample(label,frames=1):
        nonlocal offset
        send('run.step_frames',count=frames)
        with (session/'runtime.skarness.ndjson').open() as f:
            f.seek(offset)
            for line in f:
                row=json.loads(line)
                if 'topic' in row: latest[row['topic']]=row['payload']
            offset=f.tell()
        (session/(label+'.json')).write_text(json.dumps(latest,indent=2))
        return latest['replay.planning']['trip']
    try:
        caps=send('capabilities.get')['commands']
        assert {'run.step','replay.trip_plan','replay.trip_cancel'}<=set(caps)
        send('state.subscribe',topics=['replay.planning','replay.timeline','selection.state','frame.clocks','replay.prediction.controls'],detail='full')
        send('prediction.select_target',name='ship')
        send('replay.set_intercept_target',name='mars')
        send('replay.set_recording_enabled',enabled=True)
        send('replay.set_prediction_horizon',seconds=20)
        send('replay.set_prediction_enabled',enabled=True)
        for attempt in range(100):
            sample('baseline',20)
            if latest['replay.prediction.controls']['complete']: break
        assert latest['replay.prediction.controls']['complete']
        send('replay.set_trip_time_of_flight',seconds=15.9)
        send('replay.trip_plan')
        trip=sample('candidate')
        assert trip['state'] in (2,4),trip
        if converged:
            for attempt in range(100):
                trip=sample('convergence',20)
                if trip['state'] in (4,5): break
            assert trip['state']==4,trip
        else:
            assert trip['state']==2,trip
        selected=latest['selection.state']['pathTargetId']
        before=latest['frame.clocks']['sceneFrame']
        captured=latest['replay.timeline']['solver']['totalCaptured']
        assert before==0,latest['frame.clocks']
        if cancel_first:
            send('replay.trip_cancel'); assert sample('cancel')['state']==0
        send('run.step',count=1)
        assert sample('stepped')['state']==0
        assert latest['frame.clocks']['sceneFrame']==before+1
        samples=latest['replay.timeline']['solverSamples']
        assert latest['replay.timeline']['solver']['totalCaptured']==captured+1
        # Capture runs after Physics and before the Scene frame counter advances.
        frame=samples[-1]
        assert frame['sceneFrame']==before
        body=next(body for body in frame['bodies'] if body['id']==selected)
        result={key:body[key] for key in ('id','position','linearVelocity','angularVelocity')}
        (session/'result.json').write_text(json.dumps(result,indent=2))
        return result
    finally:
        try: send('session.stop')
        finally: c.close()

def run(root: Path):
    for converged in (False,True):
        name='converged' if converged else 'awaiting'
        control=scenario(root/(name+'-cancel-step'),converged,True)
        actual=scenario(root/(name+'-step'),converged,False)
        assert actual==control,(name,actual,control)
    print('PASS: AwaitingPrediction and Converged uncommitted steps match cancel-then-step exactly in identity, position and velocities')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session',type=Path,required=True)
    run(parser.parse_args().session.resolve())
