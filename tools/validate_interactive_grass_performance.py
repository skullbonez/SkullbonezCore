"""Record grass cost and bounded work separately for standard, four-view and dense scenes."""
from __future__ import annotations
import argparse
import copy
import csv
import json
import math
import statistics
from pathlib import Path
from skarness import SkarnessConnection, launch
ROOT=Path(__file__).resolve().parents[1]

def run(output: Path):
    assert not output.exists()
    output.mkdir(parents=True)
    base=json.loads((ROOT/'SkullbonezData/scenes/grass_interaction.scene.json').read_text())
    reports={}
    for mode in ('standard-live','standard-paused','four-view-live','dense-live','off-live'):
        session=output/mode;session.mkdir()
        fixture=copy.deepcopy(base)
        if mode=='dense-live':
            fixture['objects']=[]
            for i in range(100):
                body=copy.deepcopy(base['objects'][0])
                body.update(sceneObjectId=9000+i,name=f'dense_{i}',position=[507+i%10,.3,507+i//10],radius=.3,mass=.2,
                            inertia=[.0072]*3,velocity=[1,0,0],angularVelocity=[0,0,0])
                fixture['objects'].append(body)
        path=session/'fixture.scene.json';path.write_text(json.dumps(fixture,indent=2)+'\n')
        assert launch(session,ROOT/'Automation/SKULLBONEZ_CORE.exe',path,hidden=True,fixed_step=True,worker_threads=4,
                      detail='summary',perf_log=session/'perf.csv')==0
        connection=SkarnessConnection(session)
        def send(command,**arguments):
            response=connection.wait(connection.send(command,arguments))
            with (session/'commands.ndjson').open('a') as f:f.write(json.dumps(dict(command=command,arguments=arguments,response=response))+'\n')
            assert response.get('status')=='applied',response
            return response.get('result',response)
        def state():
            send('run.step_frames',count=3)
            latest={}
            for line in (session/'runtime.skarness.ndjson').read_text().splitlines():
                row=json.loads(line)
                if row.get('topic')=='ui.presentation':latest=row['payload']
            return latest
        try:
            assert 'grass.enable_fixture' in send('capabilities.get')['commands']
            send('run.pause');send('window.resize',width=2560 if mode=='four-view-live' else 1920,height=1440 if mode=='four-view-live' else 1080)
            send('grass.enable_fixture',enabled=True)
            send('render.set_parameter',index=38,value=0 if mode=='off-live' else 2)
            send('run.step',count=1)
            if mode=='four-view-live':
                ui=state();x,y,w,h=ui['headerFourViewsBounds']
                send('input.pointer_position',enabled=True,x=round(x+w/2),y=round(y+h/2));state()
                send('input.pointer_drag',button='left',x=round(x+w/2),y=round(y+h/2),deltaX=0,deltaY=0)
                assert state()['fourViews']
            send('run.step_frames',count=40)
            send('run.step_frames' if mode=='standard-paused' else 'run.step',count=240)
            observed=state()
            assert observed['grassCacheBytes']<=24*1024*1024 and observed['grassRootTests']<=131072,observed
            if mode!='off-live':assert observed['grassPatchCount']>0,observed
            send('capture.screenshot',path=str(session/'result.png'))
        finally:
            send('session.stop');connection.close()
        header=[];rows=[]
        with (session/'perf.csv').open() as stream:
            for row in csv.reader(stream):
                if row and row[0]=='pass':header=row
                elif header and len(row)==len(header):rows.append(dict(zip(header,row)))
        metrics={}
        for key in header:
            if 'Grass' not in key:continue
            # Inactive GPU markers can retain the last timestamp; they do not
            # measure an Off pass. Report only active-pass GPU observations.
            if mode=='off-live' and key.endswith('_gpu'):continue
            values=sorted(float(row[key]) for row in rows[-220:])
            if values:metrics[key]=dict(samples=len(values),p50=statistics.median(values),p95=values[math.ceil(.95*len(values))-1])
        reports[mode]=dict(metrics=metrics,counters={key:value for key,value in observed.items() if key.startswith('grass')})
        print(mode,json.dumps(reports[mode]),flush=True)
    (output/'results.json').write_text(json.dumps(reports,indent=2)+'\n')
    standard=reports['standard-live']['metrics']
    cpu=sum(standard[key]['p95'] for key in ('Frame/Render/GrassPrepare','Frame/Render/Grass','Frame/Physics/Step/Grass'))
    gpu=standard['Frame/Render/Grass_gpu']['p95']
    (output/'targets.json').write_text(json.dumps(dict(cpuP95SumMs=cpu,gpuP95Ms=gpu,cpuTargetMs=.25,gpuTargetMs=1,
        cpuWithinTarget=cpu<=.25,gpuWithinTarget=gpu<=1),indent=2)+'\n')
    print('PASS: bounded grass performance evidence recorded; hardware targets reported separately')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--session',type=Path,required=True)
    run(parser.parse_args().session.resolve())
