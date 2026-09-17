"""Native Physics dock containment, retained scroll and historical input isolation."""
from __future__ import annotations
import argparse
import json
import time
from pathlib import Path
from skarness import SkarnessConnection, launch
ROOT=Path(__file__).resolve().parents[1]


def run(session: Path):
    assert not session.exists()
    assert launch(session,ROOT/'Automation/SKULLBONEZ_CORE.exe',ROOT/'SkullbonezData/scenes/grass_interaction.scene.json',hidden=True,fixed_step=True,allocation_guard='gameplay')==0
    connection=SkarnessConnection(session)
    latest={};offset=0;checks=[]
    def send(command,**args):
        result=connection.wait(connection.send(command,args))
        with (session/'commands.ndjson').open('a') as stream:
            stream.write(json.dumps(dict(command=command,args=args,result=result))+'\n')
        assert result.get('status')=='applied',result
        return result.get('result',result)
    def state():
        nonlocal offset
        send('run.step_frames',count=3)
        with (session/'runtime.skarness.ndjson').open() as stream:
            stream.seek(offset)
            for line in stream:
                event=json.loads(line)
                if 'topic' in event: latest[event['topic']]=event['payload']
            offset=stream.tell()
        return latest['ui.presentation']
    def click(x,y,hold=0):
        send('input.pointer_position',enabled=True,x=round(x),y=round(y))
        state();time.sleep(.22);state()
        send('input.pointer_drag',button='left',x=round(x),y=round(y),deltaX=0,deltaY=0,holdMilliseconds=hold)
    def center(bounds):
        x,y,w,h=bounds;assert w>0 and h>0,bounds
        click(x+w/2,y+h/2)
    def section(index):
        ui=state();x,y,w,h=ui['physicsHeaderBounds']
        click(x+(index%2+.5)*w/2,y+66+27*(index//2))
        ui=state();assert ui['physicsSection']==index,(index,ui['physicsSection'],ui['layout'],ui['physicsHeaderBounds'],ui['physicsPeer'])
        return ui
    try:
        capabilities=send('capabilities.get')['commands']
        assert all(c in capabilities for c in ('window.resize','replay.seek_frame','input.pointer_drag'))
        send('run.pause');send('state.subscribe',topics=[],detail='normal')
        send('editor.set_enabled',enabled=True)
        send('scene.object.select',scope='editor',sceneObjectId=8101)
        send('editor.set_enabled',enabled=False)
        send('run.step',count=120)
        ui=state()
        if not ui['physicsPeer'] or ui['physicsHeaderBounds'][2]<=0:center(ui['headerPhysicsBounds'])
        for width,height in ((1680,1050),(1280,800),(640,480)):
            send('window.resize',width=width,height=height)
            for layout in ('Canvas','Editor'):
                ui=state()
                if ui['layout']!=layout:center(ui['headerLayoutBounds'])
                ui=state()
                if not ui['physicsPeer'] or ui['physicsHeaderBounds'][2]<=0:center(ui['headerPhysicsBounds'])
                assert state()['layout']==layout
                for index in range(4):
                    ui=section(index)
                    x,y,w,h=ui['physicsControlsBounds']
                    hx,hy,hw,hh=ui['physicsHeaderBounds']
                    assert w>100 and h>0 and x>=0 and y>=0,(width,layout,ui)
                    assert x+w<=width+1 and y+h<=height+1,(width,layout,(x,y,w,h))
                    assert hx>=0 and hy>=0 and hx+hw<=width+1 and hy+hh<=height+1
                    camera=dict(latest['camera.state']);steps=ui['physicsCompletedSteps']
                    send('input.pointer_wheel',x=round(x+30),y=round(y+h/2),wheelDelta=-600)
                    ui=state();retained=ui['physicsScroll']
                    assert retained>=0
                    if (640,1120,2400,520)[index]>h: assert retained>0,(width,layout,index,h,retained)
                    assert latest['camera.state']['primaryEye']==camera['primaryEye']
                    assert latest['camera.state']['primaryView']==camera['primaryView']
                    assert ui['physicsCompletedSteps']==steps
                    section((index+1)%4)
                    assert section(index)['physicsScroll']==retained
                    screenshot=session/f'{width}-{layout}-{index}.png'
                    send('capture.screenshot',path=str(screenshot))
                    if index==2:
                        # A selected body must still paint readable inspector rows
                        # at the retained scroll position, including the bottom.
                        from PIL import Image
                        with Image.open(screenshot) as image:
                            crop=image.convert('RGB').crop((round(x),round(y),round(x+w),round(y+h)))
                            text_pixels=sum(1 for red,green,blue in crop.getdata() if min(red,green,blue)>120)
                        assert text_pixels>30,(width,layout,retained,text_pixels)
                    checks.append(dict(width=width,layout=layout,section=index,scroll=retained,cameraUnchanged=True))
                ui=state();center(ui['headerFourViewsBounds']);assert state()['fourViews']
                section(2)
                send('capture.screenshot',path=str(session/f'{width}-{layout}-four.png'))
                ui=state();center(ui['headerFourViewsBounds']);assert not state()['fourViews']
        send('window.resize',width=1280,height=800)
        send('replay.seek_frame',frame=30)
        ui=section(0);before=dict(latest['scene.state']);settings=list(ui['physicsSettings']);steps=ui['physicsCompletedSteps']
        x,y,w,h=ui['physicsHeaderBounds']
        for button in range(3):click(x+(button+.5)*w/3,y+35,250)
        ui=state()
        assert ui['physicsCompletedSteps']==steps,(steps,ui['physicsCompletedSteps'])
        assert latest['scene.state']['manualResetCount']==before['manualResetCount']
        assert latest['scene.state']['pauseLocked']==before['pauseLocked']
        x,y,w,h=ui['physicsControlsBounds']
        send('input.pointer_wheel',x=round(x+20),y=round(y+h/2),wheelDelta=10000)
        ui=state();click(x+w-1,y+36)
        assert state()['physicsSettings']==settings
        send('capture.screenshot',path=str(session/'historical-read-only.png'))
        checks.append(dict(historicalTransportRejected=True,historicalSettingsRejected=True))
    finally:
        (session/'checks.json').write_text(json.dumps(checks,indent=2)+'\n')
        try:send('session.stop')
        finally:connection.close()
    print(f'PASS: {len(checks)} Physics layout, scroll, camera and historical isolation checks')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session',type=Path,required=True)
    run(parser.parse_args().session.resolve())
