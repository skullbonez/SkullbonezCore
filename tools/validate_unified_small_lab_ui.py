"""Verify small Solver Lab transport, later events, and foreground library input."""
from pathlib import Path
import argparse,json,time
from PIL import Image
from skarness import launch,SkarnessConnection
REPO=Path(__file__).resolve().parents[1]
def run(session):
 session=session.resolve()
 assert launch(session,REPO/'Automation/SKULLBONEZ_CORE.exe',REPO/'SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json',hidden=True)==0
 c=SkarnessConnection(session);latest={};offset=0
 def send(command,**args):
  r=c.wait(c.send(command,args));assert r.get('status')=='applied',r;return r
 def sample(label):
  nonlocal offset
  send('run.step_frames',count=3)
  with (session/'runtime.skarness.ndjson').open() as f:
   f.seek(offset)
   for line in f:
    e=json.loads(line)
    if 'topic' in e:latest[e['topic']]=e['payload']
   offset=f.tell()
  latest['comparison']=send('comparison.state')['result']['comparison']
  (session/(label+'.json')).write_text(json.dumps(latest,indent=2));return latest['ui.presentation']
 def click(x,y):send('input.pointer_drag',button='left',x=int(x),y=int(y),deltaX=0,deltaY=0)
 def capture(label):
  p=session/(label+'.png');send('capture.screenshot',path=str(p))
  with Image.open(p) as img:img.save(session/(label+'-view.png'))
 try:
  assert 'comparison.load' in send('capabilities.get')['commands']
  send('state.subscribe',topics=[],detail='normal')
  send('comparison.load',path=str(REPO/'SkullbonezData/solver-lab/wall-only/comparison.json'))
  ui=sample('loaded')
  for layout in ('Editor','Canvas'):
   send('window.resize',width=320,height=240);ui=sample(layout+'-size')
   if ui['layout']!=layout:click(240,18);ui=sample(layout+'-layout')
   assert ui['layout']==layout,ui
   if ui['replayControlsBounds'][2]==0:
    x,y,w,h=ui['replayDetailsBounds'];click(x+w/2,y+h/2);ui=sample('details')
   tx,ty,tw,th=ui['transportBounds'];sx,sy,sw,sh=latest['comparison']['timeline']
   assert sw>0 and tx<=sx and sx+sw<=tx+tw,(ui,latest['comparison'])
   send('comparison.seek',tick=200)
   scale=tw/220 if tw<220 else 1
   click(tx+14*scale,ty+th/2);ui=sample(layout+'-prev');assert latest['comparison']['tick']==199
   click(tx+94*scale,ty+th/2);ui=sample(layout+'-next');assert latest['comparison']['tick']==200
   click(tx+54*scale,ty+th/2);ui=sample(layout+'-play');assert latest['comparison']['direction']==1
   click(tx+54*scale,ty+th/2);ui=sample(layout+'-pause');assert latest['comparison']['direction']==0
   click(sx+sw*.75,sy+sh/2);ui=sample(layout+'-seek')
   expected=int((int(sx+sw*.75)-sx)/sw*latest['comparison']['lastTick'])
   assert abs(latest['comparison']['tick']-expected)<=1,latest['comparison']
   capture(layout+'-transport')
   send('comparison.setting',name='selectedOnly',value=0)
   send('comparison.setting',name='differencesOnly',value=0)
   ui=sample('all-events')
   if ui['causeControlsBounds'][2]==0:
    x,y,w,h=ui['detailsCausesTabBounds'];click(x+w/2,y+h/2);ui=sample('differences-tab')
   dx,dy,dw,dh=ui['causeControlsBounds'];assert dw>0 and dh>0,ui
   send('input.pointer_wheel',x=int(dx+2),y=int(dy+dh/2),wheelDelta=12000);ui=sample('events-top')
   scroll=min(144,max(0,450-dh))
   send('input.pointer_wheel',x=int(dx+2),y=int(dy+dh/2),wheelDelta=-480);ui=sample('events-visible')
   ex=dx+dw/2;ey=dy+176-scroll+12
   assert dy<=ey<dy+dh,(dy,dh,ey)
   click(ex,ey);ui=sample(layout+'-first-event');first=latest['comparison']['selectedEvent'];assert first>=0
   for n in range(5):send('input.pointer_wheel',x=int(ex),y=int(ey),wheelDelta=-120);ui=sample('events-scrolled-'+str(n))
   click(ex,ey);ui=sample(layout+'-later-event')
   assert latest['comparison']['selectedEvent']>first+1 and latest['comparison']['selected']>0,latest['comparison']
   capture(layout+'-later-event')
   # Canvas switches from Differences to Controls through the shared Details tabs.
   if ui['replayControlsBounds'][2]==0:
    x,y,w,h=ui['detailsCausesTabBounds'];click(x-w,y+h/2);ui=sample('controls-tab')
   lx,ly,lw,lh=ui['replayControlsBounds'];assert lw>0,ui
   send('input.pointer_wheel',x=int(lx+2),y=int(ly+lh/2),wheelDelta=12000);ui=sample('library-top')
   for option,name in ((0,'ragdoll-wall'),(1,'wall-only')):
    click(lx+lw/2,ly+44);ui=sample(layout+'-library-'+str(option))
    assert latest['comparison']['libraryPopupOpen'],latest['comparison']
    px,py,pw,ph=latest['comparison']['libraryPopup'];assert py>=0 and py+ph<=240
    capture(layout+'-library-popup-'+str(option))
    click(px+pw/2,py+(option+.5)*ph/2)
    for n in range(600):
     time.sleep(.1)
     ui=sample('library-loading')
     if latest['comparison']['active'] and name in latest['comparison']['bundle']:break
    assert name in latest['comparison']['bundle'] and latest['comparison']['active'],latest['comparison']
   capture(layout+'-library-loaded')
  print('PASS: 320x240 both layouts native step/play/seek, later event identity, both foreground library options')
 finally:
  try:send('session.stop')
  finally:c.close()
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--session',type=Path,required=True);run(p.parse_args().session)
