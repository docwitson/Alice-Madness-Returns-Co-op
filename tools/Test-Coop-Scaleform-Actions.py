from pathlib import Path
import struct
src=Path(__file__).with_name('Generate-Coop-Scaleform.py').read_text()
ns={};exec(src[:src.index('output=Path')],ns)
def run(b,context):
 stack=[];q=0;calls=[]
 def pop():
  assert stack,'AVM1 stack underflow';return stack.pop()
 while q<len(b):
  op=b[q];q+=1;n=struct.unpack_from('<H',b,q)[0] if op>=128 else 0;q+=2 if op>=128 else 0;d=b[q:q+n];q+=n
  if op==0x96:
   k=0
   while k<len(d):
    t=d[k];k+=1
    if t==0:z=d.index(0,k);v=d[k:z].decode();k=z+1
    elif t==7:v=struct.unpack_from('<i',d,k)[0];k+=4
    elif t==5:v=bool(d[k]);k+=1
    elif t==3:v=None
    else:raise AssertionError(t)
    stack.append(v)
  elif op==0x1c:stack.append(context.get(pop()))
  elif op==0x1d:v=pop();context[pop()]=v
  elif op==0x4e:key=pop();obj=pop();stack.append(obj[key])
  elif op==0x4f:v=pop();key=pop();obj=pop();obj[key]=v
  elif op==0x42:
   n=pop();stack.append([pop() for _ in range(n)])
  elif op==0x49:v=pop();stack.append(pop()==v)
  elif op==0x12:stack.append(not pop())
  elif op==0x9d:
   if pop():q+=struct.unpack('<h',d)[0]
  elif op==0x99:q+=struct.unpack('<h',d)[0]
  elif op==0x17:pop()
  elif op==0x3d:
   name=pop();n=pop();args=[pop() for _ in range(n)];calls.append(name);stack.append(None)
  elif op==0x52:
   name=pop();obj=pop();n=pop();args=[pop() for _ in range(n)];calls.append(name);stack.append(None)
  elif op==0x3e:pop();assert not stack;return True,calls
  else:raise AssertionError(hex(op))
 assert not stack
 return False,calls

def context(label,open=False,section='menu',host=False,confirm=False,canJoin=True,canSync=True,about=False):
 return {'_global':{'AliceCoopOpen':open,'AliceCoopHost':host,'AliceCoopConfirm':confirm,
   'AliceCoopAbout':about,
   'AliceCoopInteractive':True,'AliceCoopCanJoin':canJoin,'AliceCoopCanSync':canSync,'AliceCoopProfileToken':7,'AliceCoopProfile':'Test profile'},
   'section':section,'menuBtns':[label],'currentId':{'menu':0},'sfm':17,'menu_btn_0':{'highlight':False},'menu_btn_1':{'highlight':False},'menu_btn_4':{'highlight':False},'selectedButton':{'deleted':True}}
c=context('Multiplayer');handled,calls=run(ns['parts']['EnterPrefix'],c);assert handled and c['_global']['AliceCoopOpen'] and calls==['clearInterval','CreateMainMenu']
c=context('Join host',True);handled,calls=run(ns['parts']['EnterPrefix'],c);assert handled and c['_global']['AliceCoopCommand']==1 and not calls
c=context('Join host',True,canJoin=False);run(ns['parts']['EnterPrefix'],c);assert 'AliceCoopCommand' not in c['_global']
c=context('Receive host profile',True);handled,calls=run(ns['parts']['EnterPrefix'],c);assert handled and c['_global']['AliceCoopConfirm'] and calls==['clearInterval','CreateMainMenu']
assert 'AliceCoopCommand' not in c['_global'] and c['_global']['AliceCoopConfirmedProfileToken']==7 and c['currentId']['menu']==1
# Rebuilding creates these labels in the real movie. Repeated Enter must go back,
# even before native setFirstMenu's delayed focus animation runs.
c['menuBtns']=['Receive and replace','Back'];run(ns['parts']['EnterPrefix'],c)
assert not c['_global']['AliceCoopConfirm'] and c['_global']['AliceCoopOpen'] and 'AliceCoopCommand' not in c['_global']
c=context('Receive host profile',True,canSync=False);run(ns['parts']['EnterPrefix'],c);assert not c['_global']['AliceCoopConfirm'] and 'AliceCoopCommand' not in c['_global']
c=context('Receive and replace',True,confirm=True);handled,calls=run(ns['parts']['EnterPrefix'],c);assert handled and c['_global']['AliceCoopCommand']==3 and not c['_global']['AliceCoopConfirm']
c=context('Back',True,confirm=True);handled,calls=run(ns['parts']['EnterPrefix'],c);assert handled and not c['_global']['AliceCoopConfirm'] and c['_global']['AliceCoopOpen'] and 'AliceCoopCommand' not in c['_global']
for label in ['Join host','Receive host profile']:
 c=context(label,True,host=True);run(ns['parts']['EnterPrefix'],c);assert 'AliceCoopCommand' not in c['_global'] and not c['_global']['AliceCoopConfirm']
for host,confirm,expected in [(True,False,['About','Back']),(False,False,['Join host','Receive host profile','About','Back']),(False,True,['Receive and replace','Back'])]:
 c=context('Unused',True,host=host,confirm=confirm);run(ns['parts']['ChooseSubmenu'],c);assert c['menuBtns']==expected
c=context('Back',True);handled,calls=run(ns['parts']['EnterPrefix'],c);assert handled and not c['_global']['AliceCoopOpen'] and calls==['clearInterval','CreateMainMenu']
c=context('FlashUI_NewGame');handled,calls=run(ns['parts']['EnterPrefix'],c);assert not handled and not calls
c=context('Multiplayer');c['bOpenPlayerInfo']=True;handled,calls=run(ns['parts']['EnterPrefix'],c);assert not handled and not calls and not c['_global']['AliceCoopOpen']
c=context('Multiplayer',False,'cust');handled,calls=run(ns['parts']['EnterPrefix'],c);assert not handled and not calls
c=context('Unused',True,confirm=True);handled,calls=run(ns['parts']['BackPrefix'],c);assert handled and not c['_global']['AliceCoopConfirm'] and c['_global']['AliceCoopOpen']
c=context('Unused',True);handled,calls=run(ns['parts']['BackPrefix'],c);assert handled and not c['_global']['AliceCoopOpen']
c=context('FlashUI_ExitGame');handled,calls=run(ns['parts']['BackPrefix'],c);assert not handled and not calls
print('Native menu actions passed: role-specific items, eligibility, explicit confirmation, Back at bottom/default, held Enter, native passthrough; balanced AVM1 stack.')

for host in [False,True]:
 c=context('About',True,host=host);handled,calls=run(ns['parts']['EnterPrefix'],c)
 assert handled and c['_global']['AliceCoopAbout'] and c['_global']['AliceCoopOpen'] and calls==['clearInterval','CreateMainMenu']
 assert 'AliceCoopCommand' not in c['_global'] and not c['_global']['AliceCoopConfirm']
 assert c['currentId']['menu']==4 and c['selectedButton'] is c['menu_btn_4']
 run(ns['parts']['ChooseSubmenu'],c);assert c['menuBtns']==ns['social_labels']+['Back']
 run(ns['parts']['EnterPrefix'],c)
 assert not c['_global']['AliceCoopAbout'] and c['_global']['AliceCoopOpen'] and c['selectedButton'] is c['menu_btn_0']
 c=context('Unused',True,host=host,about=True);run(ns['parts']['BackPrefix'],c)
 assert not c['_global']['AliceCoopAbout'] and c['_global']['AliceCoopOpen']
 for label in ['Join host','Receive host profile','Receive and replace']:
  c=context(label,True,host=host,about=True);run(ns['parts']['EnterPrefix'],c)
  assert 'AliceCoopCommand' not in c['_global'] and c['_global']['AliceCoopAbout'] and not c['_global']['AliceCoopConfirm']
 for index,label in enumerate(ns['social_labels']):
  c=context(label,True,host=host,about=True,canJoin=False,canSync=False)
  handled,calls=run(ns['parts']['EnterPrefix'],c)
  assert handled and not calls and c['_global']['AliceCoopCommand']==30+index and c['_global']['AliceCoopAbout'] and c['_global']['AliceCoopOpen']
  c=context(label,True,host=host);run(ns['parts']['EnterPrefix'],c);assert 'AliceCoopCommand' not in c['_global']
print('About passed for both roles: four isolated link commands, Back default protects held Enter, escape returns to Multiplayer, focus restored.')

for interactive in [False,True]:
 c=context('Unused');c['_global']['AliceCoopInteractive']=interactive
 c['_root.titleScreen']={'_currentframe':150,'_visible':True};c['_root.profile']={'_visible':True};c['_root.profile.content']={};c['Key']={};c['flash.external.ExternalInterface']={}
 handled,calls=run(ns['isolate_screens'](),c)
 assert not handled
 assert calls==(['call','removeListener','stop','removeListener'] if interactive else [])
 assert c['_root.profile']['_visible']==(not interactive)
c=context('Unused');c['this']={'_visible':False};run(ns['parts']['ProfileEnter'],c);assert c['this']['_visible']
c=context('Unused');c['_global']['AliceCoopInteractive']=True;run(ns['parts']['StageLeave'],c);assert not c['_global']['AliceCoopInteractive']
print('Native lifecycle passed: no cleanup during startup; active menu releases sibling listeners; profile reopens; leaving releases menu ownership.')

c=context('Multiplayer');c['_global']['AliceCoopInteractive']=False;handled,calls=run(ns['parts']['EnterPrefix'],c);assert handled and not calls and not c['_global']['AliceCoopOpen']
print('Multiplayer cannot open during the title/profile entrance animation.')

c=context('Unused');c['_global']['AliceCoopInteractive']=False
c['_root.titleScreen']={'_currentframe':160,'_visible':True};c['_root.profile']={'_visible':True};c['_root.profile.content']={};c['Key']={};c['flash.external.ExternalInterface']={}
_,calls=run(ns['parts']['StageEntrance'],c)
assert calls==['removeListener','stop','removeListener'] and not c['_root.profile']['_visible'] and not c['_root.titleScreen']['_visible']
assert not c['_global']['AliceCoopInteractive']
print('Native menu entrance hides outgoing profile/title without prematurely enabling input.')

# Native setFirstMenu highlights by currentId but does not assign selectedButton.
# The first Down must deselect the newly created first button on every rebuilt page.
for label, opened, confirm in [('Multiplayer',False,False),('Receive host profile',True,False),('Back',True,True),('Back',True,False)]:
 c=context(label,opened,confirm=confirm)
 run(ns['parts']['EnterPrefix'],c)
 first=c['menu_btn_1'] if c['_global']['AliceCoopConfirm'] else c['menu_btn_0']
 assert c['selectedButton'] is first, label
 first['highlight']=True
 second={'highlight':False}
 c['selectedButton']['highlight']=False
 c['currentId']['menu']=1; c['selectedButton']=second; second['highlight']=True
 assert not first['highlight'] and second['highlight'], label
c={'this':{'_visible':False}}
run(ns['parts']['TitleEnter'],c);assert c['this']['_visible']
print('Initial focus reference passed for Multiplayer, confirmation and Back; only one highlight after first navigation. Native title visibility restores on entry.')

c=context('Unused');c['_global']['AliceCoopInteractive']=True
c['_root.titleScreen']={'_currentframe':150,'_visible':False};c['_root.profile']={'_visible':False};c['_root.profile.content']={};c['Key']={};c['flash.external.ExternalInterface']={}
_,calls=run(ns['isolate_screens'](),c);assert calls==['removeListener','stop','removeListener']
print('Repeated refresh does not replay TitleOnStartPressed on an already hidden title.')
