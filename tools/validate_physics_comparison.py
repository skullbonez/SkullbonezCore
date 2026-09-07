"""Exercise linked comparison camera, layouts, pointer capture and finding restoration.

Inputs are a small A/A sphere bundle (stable object 1) and a large wall bundle.
Both bundles remain read-only; screenshots and command results go to --session.
"""
import argparse
from pathlib import Path
import json,sys,time
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parent))
from skarness import launch,SkarnessConnection
from PIL import Image
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--exe',type=Path,default=root/'Automation/SKULLBONEZ_CORE.exe')
parser.add_argument('--aa-bundle',type=Path,required=True)
parser.add_argument('--large-bundle',type=Path,required=True)
parser.add_argument('--session',type=Path,required=True)
args=parser.parse_args()
session=args.session.resolve()
if session.exists():raise SystemExit('Use a new session directory to preserve earlier evidence')
launch(session,args.exe.resolve(),root/'SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json',hidden=True,fixed_step=True)
c=SkarnessConnection(session);results=[]
def cmd(command_name,**args):
 result=c.wait(c.send(command_name,args));results.append({'command':command_name,'arguments':args,'result':result});print(command_name,json.dumps(result),flush=True)
 assert result.get('status')=='applied',result
 return result.get('result',{})
def state():return cmd('comparison.state')['comparison']
try:
 caps=c.wait(c.send('capabilities.get',{}));assert 'input.set_movement' in caps['commands']
 cmd('state.subscribe',topics=['frame.clocks'],detail='summary');cmd('run.pause')

 # A large load must leave the UI/pipe responsive and be cancellable.
 pending=c.send('comparison.load',{'path':str(args.large_bundle.resolve())})
 started=time.monotonic();loading=state();assert loading['loading'];assert time.monotonic()-started<2.0
 cmd('capture.screenshot',path=str(session/'loading.png'))
 width,height=Image.open(session/'loading.png').size
 cmd('input.pointer_drag',button='left',x=int((width-560)/2+510),y=int((height-150)/2+120),deltaX=0,deltaY=0)
 deadline=time.monotonic()+8
 while state()['loading']:
  assert time.monotonic()<deadline,'Cancellation did not finish promptly'
  time.sleep(.05)
 assert state()['lastTick']==0
 cmd('comparison.load',path=str(args.aa_bundle.resolve()))
 cmd('comparison.seek',tick=1)
 for stacked,label in [(0,'side-by-side'),(1,'stacked')]:
  cmd('comparison.setting',name='stackedViews',value=stacked)
  cmd('capture.screenshot',path=str(session/(label+'.png')))
  s=state();assert s['stackedViews']==bool(stacked) and s['tick']==1
 width,height=Image.open(session/'stacked.png').size
 # Move down off the thin timeline while preserving horizontal dragging.
 start=433;end=start+150;span=width-605
 cmd('input.pointer_drag',button='left',x=start,y=height-31,deltaX=150,deltaY=-150,moveClient=True)
 s=state();expected=int((end-383)/span*s['lastTick']);assert abs(s['tick']-expected)<=1,(s,expected)
 assert not s['timelineDragging']
 # Starting a drag outside the timeline must not acquire it mid-hold.
 held=s['tick'];cmd('input.pointer_drag',button='left',x=start,y=height-130,deltaX=20,deltaY=99,moveClient=True)
 assert state()['tick']==held
 cmd('comparison.seek',tick=1);cmd('comparison.select',sceneObjectId=1);cmd('comparison.focus')
 before=state();assert before['orbitSelected']
 cmd('input.pointer_drag',button='right',x=300,y=200,deltaX=45,deltaY=15)
 orbit=state();assert orbit['cameraEye']!=before['cameraEye'] and orbit['cameraView']==before['cameraView']
 cmd('input.set_movement',w=True,a=False,s=False,d=False);time.sleep(.18)
 moving=state();cmd('input.set_movement',w=False,a=False,s=False,d=False)
 assert not moving['orbitSelected'] and moving['selected']==1 and moving['cameraEye']!=orbit['cameraEye']
 before=state();cmd('input.pointer_drag',button='right',x=300,y=200,deltaX=30,deltaY=10)
 free=state();assert free['cameraEye']==before['cameraEye'] and free['cameraView']!=before['cameraView']
 f=np.array(before['cameraView'])-np.array(before['cameraEye']);f=f/np.linalg.norm(f)
 right=np.cross(f,[0,1,0]);right=right/np.linalg.norm(right)
 up=np.cross(right,f);new=np.array(free['cameraView'])-np.array(free['cameraEye']);new=new/np.linalg.norm(new)
 assert np.dot(new,right)>0 and np.dot(new,up)<0,(f,new)

 cmd('comparison.select',sceneObjectId=1);cmd('comparison.focus')
 # Click the selected object's projected centre in the top viewport.
 cmd('comparison.select',sceneObjectId=0)
 cmd('input.pointer_drag',button='left',x=(width-390)//2,y=82+(height-139)//4,deltaX=0,deltaY=0)
 picked=state();assert picked['selected']==1 and picked['orbitSelected'],picked
 for stacked,label in [(0,'round-side'),(1,'round-stack')]:
  cmd('comparison.setting',name='stackedViews',value=stacked);cmd('comparison.focus')
  cmd('capture.screenshot',path=str(session/(label+'.png')))
  data=np.array(Image.open(session/(label+'.png')).convert('RGB'))
  w=width-390;h=(height-139)//2*2
  if not stacked:w=w//2
  else:h=h//2
  crop=data[82:82+h,:w];mask=np.max(crop,axis=2)>15
  left=right=w//2;top=bottom=h//2;assert mask[top,left]
  while left>0 and mask[h//2,left-1]:left-=1
  while right+1<w and mask[h//2,right+1]:right+=1
  while top>0 and mask[top-1,w//2]:top-=1
  while bottom+1<h and mask[bottom+1,w//2]:bottom+=1
  ratio=(right-left+1)/(bottom-top+1)
  assert .98<ratio<1.02,(label,ratio)
 picked=state()
 cmd('comparison.finding.save',path=str(session/'finding.json'),note='Feedback camera and layout restoration')
 cmd('comparison.setting',name='stackedViews',value=0);cmd('comparison.select',sceneObjectId=0)
 cmd('comparison.finding.load',path=str(session/'finding.json'));restored=state()
 assert restored['stackedViews'] and restored['orbitSelected'] and restored['selected']==1
 assert restored['cameraEye']==picked['cameraEye']
 # Every display mode preserves the cursor and linked camera. Controlled A/A
 # pixel output excludes panel labels and must be exactly black.
 for mode in ('split','overlay','toggle','heatmap','pixels'):
  before=state();cmd('comparison.mode',mode=mode)
  current=state();assert current['tick']==before['tick'] and current['cameraEye']==before['cameraEye']
  cmd('capture.screenshot',path=str(session/(mode+'.png')))
 pixels=np.array(Image.open(session/'pixels.png').convert('RGB'))
 assert np.max(pixels[140:height-58,2:width-392])==0,'A/A controlled pixel difference is not zero'
 cmd('comparison.mode',mode='split')
 # The first contact row belongs to the selected ball. Clicking it seeks but
 # must not translate the linked camera, including clicking the same row again.
 cmd('comparison.setting',name='differencesOnly',value=0)
 cmd('comparison.select',sceneObjectId=1)
 for repeat in range(2):
  cmd('comparison.seek',tick=120);before=state()
  cmd('input.pointer_drag',button='left',x=width-350,y=222,deltaX=0,deltaY=0)
  after=state();assert after['tick']!=120 and after['selected']==1
  assert after['cameraEye']==before['cameraEye'] and after['cameraView']==before['cameraView'],'Causal selection reframed the camera'
 print('FEEDBACK QA PASS',flush=True)
finally:
 Path(session/'checks.json').write_text(json.dumps(results,indent=2))
 try:cmd('session.stop')
 finally:c.close()
