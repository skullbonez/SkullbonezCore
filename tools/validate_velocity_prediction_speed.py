"""Compare ordinary and velocity-modified prediction settings and completion pace."""
from __future__ import annotations
import argparse
import json
import time
from pathlib import Path
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    assert launch(session, REPO/'Automation/SKULLBONEZ_CORE.exe',
                  REPO/'SkullbonezData/scenes/space_field_200.scene.json', hidden=True,
                  worker_threads=4, layout_file=session/'layout.preferences', allocation_guard='gameplay')==0
    connection=SkarnessConnection(session)
    latest={}
    offset=0
    passed=False
    timings=[]
    build_turns=[]
    building=False
    build_start=0

    def send(command, **args):
        result=connection.wait(connection.send(command,args))
        assert result.get('status')=='applied',(command,result)
        return result

    def sample():
        nonlocal offset, building, build_start
        send('run.step_frames',count=2)
        with (session/'runtime.skarness.ndjson').open('rb') as stream:
            stream.seek(offset)
            for line in stream:
                if not line.endswith(b'\n'): break
                offset+=len(line)
                row=json.loads(line)
                if 'topic' in row: latest[row['topic']]=row['payload']
                if row.get('topic')=='replay.prediction.controls':
                    active=row['payload']['building']
                    if active and not building: build_start=row['runtimeTurn']
                    if building and not active and row['payload']['complete']:
                        build_turns.append(row['runtimeTurn']-build_start)
                    building=active
        return latest['replay.state'],latest['replay.visual_packet']

    def complete(label):
        start=time.monotonic()
        built=None
        while time.monotonic()-start<90:
            state,packet=sample()
            controls=latest['replay.prediction.controls']
            assert controls['revealRate']==1000 and controls['highDetail'] and controls['horizonSeconds']==20, controls
            if state['predictionComplete'] and built is None: built=time.monotonic()-start
            if state['predictionComplete'] and not state['causeLoading'] and packet['header']['revealFrame']>=2400:
                assert state['pathTargetId']==state['publishedPredictionTargetId']==state['submittedPredictionTargetId']==1
                assert state['publishedPredictionFrames']==2401
                assert state['selectedFutureRootPointCount']>=2
                timings.append({'label':label,'buildSeconds':built,'visibleSeconds':time.monotonic()-start,
                                'frames':state['publishedPredictionFrames'],'updateFrames':build_turns[-1],'activePath':packet['activePath']})
                send('capture.screenshot',path=str(session/(label+'.png')))
                return state,packet
        raise AssertionError('Prediction did not complete: '+label)

    try:
        assert {'replay.velocity_preview','replay.velocity_commit','replay.set_reveal_speed'}<=set(send('capabilities.get')['commands'])
        send('state.subscribe',topics=[],detail='summary')
        send('replay.set_prediction_horizon',seconds=20)
        send('replay.set_prediction_detail',highDetail=True)
        send('replay.set_reveal_speed',rate=1000)
        send('prediction.select_target',name='field_000')
        send('replay.set_prediction_enabled',enabled=True)
        _,original=complete('original')
        send('replay.set_velocity_edit_enabled',enabled=True)
        send('run.step_frames',count=12)
        _,original=sample()
        for index,speed in enumerate((-0.01,0.01)):
            send('replay.velocity_preview',linear=[speed,0,0.196],angular=[0,0,0])
            state,held=sample()
            assert not state['predictionGenerationPermitted']
            assert held['originalPath']['geometryHash']==original['activePath']['geometryHash']
            assert held['originalPath']['records']==original['activePath']['records']
            send('replay.velocity_commit')
            state,modified=complete('modified-'+str(index))
            assert state['divergence']['redReady'] and modified['activePath']['allRed']
            assert state['divergence']['blueBodies']['omitted'] and state['divergence']['blueBodies']['count']==200
            assert timings[-1]['updateFrames'] <= timings[0]['updateFrames']*1.25, timings
            assert modified['originalPath']==held['originalPath']
            # Equal settings are exact. Timing allows scheduling noise and changed
            # collision work, while rejecting the former 20-second reveal floor.
            assert timings[-1]['visibleSeconds']<max(8.0, timings[0]['visibleSeconds']*2.5),timings
        passed=True
        print(json.dumps({'passed':passed,'timings':timings}))
    finally:
        (session/'result.json').write_text(json.dumps({'passed':passed,'timings':timings,'completed':len(timings)==3},indent=2))
        try: send('session.stop')
        finally: connection.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session',type=Path,required=True)
    run(parser.parse_args().session.resolve())
