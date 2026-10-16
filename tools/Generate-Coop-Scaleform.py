"""Generate our AVM1 additions only; no game bytecode/assets are embedded."""
from pathlib import Path
import struct

def op(c,p=b""):
    return bytes([c])+ (struct.pack("<H",len(p))+p if c>=128 else b"")
def push(x):
    if isinstance(x,bool): p=bytes([5,x])
    elif isinstance(x,int): p=b"\x07"+struct.pack("<i",x)
    else: p=b"\x00"+x.encode()+b"\x00"
    return op(0x96,p)
def var(n):return push(n)+op(0x1c)
def glob(n):return var("_global")+push(n)+op(0x4e)
def setg(n,value):return var("_global")+push(n)+value+op(0x4f)
def setv(n,value):return push(n)+value+op(0x1d)
def setmember(obj,n,value):return var(obj)+push(n)+value+op(0x4f)
def call(n,args=()):return b"".join(reversed(args))+push(len(args))+push(n)+op(0x3d)
def method(obj,n,args=()):return b"".join(reversed(args))+push(len(args))+var(obj)+push(n)+op(0x52)
def discard(x):return x+op(0x17)
def ret():return op(0x96,b"\x03")+op(0x3e)
def cond(test,yes):return test+op(0x12)+op(0x9d,struct.pack("<h",len(yes)))+yes
def eq(a,b):return a+b+op(0x49)
def fn(n,body):return op(0x9b,n.encode()+b"\0\0\0"+struct.pack("<H",len(body)))+body

def either(test, yes, no):
    return test+op(0x12)+op(0x9d,struct.pack("<h",len(yes)+5))+yes+op(0x99,struct.pack("<h",len(no)))+no

def rebuild():
    # Reset selection before replacing buttons: a held Enter/A cannot accept the
    # destructive choice while the native delayed focus timer is still running.
    default=either(glob("AliceCoopConfirm"),push(1),either(glob("AliceCoopAbout"),push(4),push(0)))
    button=either(glob("AliceCoopConfirm"),var("menu_btn_1"),either(glob("AliceCoopAbout"),var("menu_btn_4"),var("menu_btn_0")))
    return discard(call("clearInterval",[var("sfm")]))+setv("canReloadTitle",push(True))+setmember("currentId","menu",default)+discard(call("CreateMainMenu"))+setv("selectedButton",button)
def back():
    cancel= setg("AliceCoopConfirm",push(False))+rebuild()+ret()
    about= setg("AliceCoopAbout",push(False))+rebuild()+ret()
    leave= setg("AliceCoopOpen",push(False))+setg("AliceCoopConfirm",push(False))+setg("AliceCoopAbout",push(False))+rebuild()+ret()
    return either(glob("AliceCoopAbout"),about,either(glob("AliceCoopConfirm"),cancel,leave))
def array(labels):
    return setv("menuBtns",b"".join(push(s) for s in reversed(labels))+push(len(labels))+op(0x42))

selected=var("menuBtns")+var("currentId")+push("menu")+op(0x4e)+op(0x4e)
confirm_action=cond(eq(selected,push("Back")),setg("AliceCoopConfirm",push(False))+rebuild()+ret())
confirm_action+=cond(eq(selected,push("Receive and replace")),cond(glob("AliceCoopCanSync"),setg("AliceCoopCommand",push(3))+setg("AliceCoopConfirm",push(False))+rebuild())+ret())
normal_action=cond(eq(selected,push("Back")),back())
normal_action+=cond(eq(selected,push("About")),setg("AliceCoopAbout",push(True))+rebuild()+ret())
client_actions=cond(eq(selected,push("Join host")),cond(glob("AliceCoopCanJoin"),setg("AliceCoopCommand",push(1)))+ret())
client_actions+=cond(eq(selected,push("Receive host profile")),cond(glob("AliceCoopCanSync"),setg("AliceCoopConfirmedProfileToken",glob("AliceCoopProfileToken"))+setg("AliceCoopConfirmedProfile",glob("AliceCoopProfile"))+setg("AliceCoopConfirm",push(True))+rebuild())+ret())
normal_action+=cond(glob("AliceCoopHost")+op(0x12),client_actions)+ret()
social_labels=["GitHub - Source & Releases","Nexus Mods","X","YouTube"]
about_action=cond(eq(selected,push("Back")),back())
for index,label in enumerate(social_labels):
    about_action+=cond(eq(selected,push(label)),setg("AliceCoopCommand",push(30+index))+ret())
about_action+=ret()
page_actions=either(glob("AliceCoopAbout"),about_action,either(glob("AliceCoopConfirm"),confirm_action+ret(),normal_action))
enter=cond(glob("AliceCoopOpen"),page_actions)
enter+=cond(eq(selected,push("Multiplayer")),cond(glob("AliceCoopInteractive")+op(0x12),ret())+setg("AliceCoopOpen",push(True))+setg("AliceCoopConfirm",push(False))+setg("AliceCoopAbout",push(False))+rebuild()+ret())
enter=cond(eq(var("section"),push("menu")),cond(var("bOpenPlayerInfo")+op(0x12),enter))
submenu=either(glob("AliceCoopAbout"),array(social_labels+["Back"]),
    either(glob("AliceCoopConfirm"),array(["Receive and replace","Back"]),
    either(glob("AliceCoopHost"),array(["About","Back"]),array(["Join host","Receive host profile","About","Back"]))))
choose=cond(glob("AliceCoopOpen"),submenu+op(0x99,b"\0\0"))
append=cond(glob("AliceCoopOpen")+op(0x12),discard(method("menuBtns","push",[push("Multiplayer")])))

# Reuse fonts imported by this movie: ContentFont is normal Arno Pro; the other
# aliases supply the game's bold and heading faces without a bundled font.
fields=[
    ("coop_title", "AliceCoopTitle", 118, 48, "$NormalFont", 28, True, 0xF2E5C9),
    ("coop_role", "AliceCoopRole", 184, 28, "$ContentFont", 18, False, 0xBDAF98),
    ("coop_status", "AliceCoopStatus", 221, 40, "$ContentFont", 23, True, 0xDCC996),
    ("coop_details", "AliceCoopDetails", 267, 78, "$ContentFont", 17, False, 0xD7CBB8),
    ("coop_heading", "AliceCoopHeading", 367, 34, "$NormalFont", 22, True, 0xDEC493),
    ("coop_body", "AliceCoopBody", 409, 179, "$ContentFont", 17, False, 0xD7CBB8),
    ("coop_nav", "AliceCoopNav", 614, 48, "$ContentFont", 16, False, 0xBDB2A0),
]
def field_value(name,key):
    value=either(glob("AliceCoopConfirm"),glob(key.replace("AliceCoop","AliceCoopConfirm",1)),glob(key))
    if name=="coop_status": value=either(glob("AliceCoopConfirm"),push("Profile: ")+glob("AliceCoopConfirmedProfile")+op(0x47),glob(key))
    return either(glob("AliceCoopAbout"),glob(key.replace("AliceCoop","AliceCoopAbout",1)),value)

# Own vector shapes drawn by the existing Scaleform movie. Create them before
# the fields so they stay underneath text; cleanup removes the whole clip.
panels=discard(method("this","createEmptyMovieClip",[push("coop_panels"),method("this","getNextHighestDepth")]))
for y,h in [(174,177),(358,237),(604,60)]:
    panels+=discard(method("coop_panels","lineStyle",[push(1),push(0xAB9570),push(28)]))
    panels+=discard(method("coop_panels","beginFill",[push(0x0C0E0D),push(48)]))
    panels+=discard(method("coop_panels","moveTo",[push(748),push(y)]))
    for x,yy in [(1224,y),(1224,y+h),(748,y+h),(748,y)]:
        panels+=discard(method("coop_panels","lineTo",[push(x),push(yy)]))
    panels+=discard(method("coop_panels","endFill"))
    panels+=discard(method("coop_panels","lineStyle",[push(1),push(0xDEC493),push(45)]))
    panels+=discard(method("coop_panels","moveTo",[push(768),push(y+1)]))
    panels+=discard(method("coop_panels","lineTo",[push(1204),push(y+1)]))
info=panels
for name,key,y,h,font,size,bold,color in fields:
    info+=discard(method("this","createTextField",[push(name),method("this","getNextHighestDepth"),push(768),push(y),push(436),push(h)]))
    info+=setv("coop_format",method("menu_btn_0.txt","getTextFormat"))
    for prop,value in [("font",font),("size",size),("bold",bold),("color",color)]:info+=setmember("coop_format",prop,push(value))
    info+=setmember(name,"multiline",push(True))+setmember(name,"wordWrap",push(True))+setmember(name,"embedFonts",push(True))+setmember(name,"selectable",push(False))
    info+=discard(method(name,"setNewTextFormat",[var("coop_format")]))
    value=field_value(name,key)
    info+=setmember(name,"text",value)+discard(method(name,"setTextFormat",[var("coop_format")]))

# The native customization footer belongs to this exact sprite. Preserve its
# original visibility and restore it when returning to the game's normal menu.
hide_hints=b"";restore_hints=b""
for name in ["hint_menu","hint_cust"]:
    hide_hints+=setv("coop_old_"+name,var(name)+push("_visible")+op(0x4e))+setmember(name,"_visible",push(False))
    restore_hints+=setmember(name,"_visible",var("coop_old_"+name))
hints=cond(var("coop_hintsHidden")+op(0x12),hide_hints+setv("coop_hintsHidden",push(True)))
layout=setmember("menu_btn_0","_x",push(150))+setmember("menu_btn_0","_y",push(260))
layout+=cond(glob("AliceCoopAbout")+op(0x12),setmember("menu_btn_1","_x",push(150))+setmember("menu_btn_1","_y",push(355)))
extra_buttons=b""
for index,y in [(2,450),(3,545)]:
    extra_buttons+=setmember("menu_btn_"+str(index),"_x",push(150))+setmember("menu_btn_"+str(index),"_y",push(y))
layout+=cond(glob("AliceCoopAbout")+op(0x12),cond(glob("AliceCoopConfirm")+op(0x12),cond(glob("AliceCoopHost")+op(0x12),extra_buttons)))
create_info=cond(glob("AliceCoopOpen"),hints+layout+info)
# Use native buttons for link hit-testing, focus and controller navigation.
# Bring these above the panel shapes, then add the small vector silhouettes.
social_layout=b""
for index,y in enumerate([420,463,506,549]):
    button="menu_btn_"+str(index)
    social_layout+=setmember(button,"_x",push(808))+setmember(button,"_y",push(y))
    # ScaleTo animates the whole native button back to 100/120 percent. Change
    # the text format itself so the smaller type survives selection/deselection.
    for field in [button+".txt",button+".bg.txt"]:
        social_layout+=setv("coop_link_format",method(field,"getTextFormat"))
        social_layout+=setmember("coop_link_format","size",push(18))
        social_layout+=discard(method(field,"setNewTextFormat",[var("coop_link_format")]))
        social_layout+=discard(method(field,"setTextFormat",[var("coop_link_format")]))
    width=var(button+".txt")+push("textWidth")+op(0x4e)+push(2)+op(0x0d)+push(10)+op(0x0b)
    social_layout+=setmember(button+".b_2","_x",width)
    social_layout+=discard(method(button,"swapDepths",[method("this","getNextHighestDepth")]))
social_layout+=setmember("menu_btn_4","_x",push(150))+setmember("menu_btn_4","_y",push(545))
social_layout+=discard(method("menu_btn_4","swapDepths",[method("this","getNextHighestDepth")]))
social_icons=discard(method("this","createEmptyMovieClip",[push("coop_social_icons"),method("this","getNextHighestDepth")]))
def stroke(icon,points):
    b=discard(method(icon,"moveTo",[push(points[0][0]),push(points[0][1])]))
    for x,y in points[1:]:b+=discard(method(icon,"lineTo",[push(x),push(y)]))
    return b
# Simplified cat silhouette, Nexus diamond/N, X and video/play in one stroke style.
icon_paths=[
    [[(-6,2),(-6,-4),(-6,-7),(-3,-5),(3,-5),(6,-7),(6,-4),(6,2),(3,5),(-3,5),(-6,2)], [(-2,5),(-2,8)],[(2,5),(2,8)]],
    [[(0,-7),(7,0),(0,7),(-7,0),(0,-7)],[(-2,3),(-2,-3),(2,3),(2,-3)]],
    [[(-6,-6),(6,6)],[(6,-6),(-6,6)]],
    [[(-7,-6),(7,-6),(7,6),(-7,6),(-7,-6)],[(-2,-3),(3,0),(-2,3),(-2,-3)]],
]
align_icons=b""
for index,paths in enumerate(icon_paths):
    child="icon_"+str(index);icon="coop_social_icons."+child;button="menu_btn_"+str(index)
    social_icons+=discard(method("coop_social_icons","createEmptyMovieClip",[push(child),method("coop_social_icons","getNextHighestDepth")]))
    social_icons+=setmember(icon,"_x",push(784))
    social_icons+=discard(method(icon,"lineStyle",[push(1),push(0xDEC493),push(90)]))
    for points in paths:social_icons+=stroke(icon,points)
    # Center on the actual native text (including its 2px gutter and animated
    # button scale), rather than a separate hard-coded icon baseline.
    center=var(button+".txt")+push("_y")+op(0x4e)+push(2)+op(0x47)
    center+=var(button+".txt")+push("textHeight")+op(0x4e)+push(2)+op(0x0d)+op(0x47)
    center+=var(button)+push("_yscale")+op(0x4e)+op(0x0c)+push(100)+op(0x0d)
    center+=var(button)+push("_y")+op(0x4e)+op(0x47)
    align_icons+=setmember(icon,"_y",center)
social_icons+=align_icons
create_info+=cond(glob("AliceCoopOpen"),cond(glob("AliceCoopAbout"),social_layout+social_icons))
clear_info=b"".join(discard(method(name,"removeTextField")) for name in ["coop_info"]+[f[0] for f in fields])
clear_info+=discard(method("coop_panels","removeMovieClip"))
clear_info+=discard(method("coop_social_icons","removeMovieClip"))
clear_info+=cond(glob("AliceCoopOpen")+op(0x12),cond(var("coop_hintsHidden"),restore_hints+setv("coop_hintsHidden",push(False))))
refresh=b""
for name,key,*_ in fields:
    value=field_value(name,key)
    refresh+=setmember(name,"text",value)
refresh+=setv("coop_status_format",method("coop_status","getTextFormat"))
refresh+=setmember("coop_status_format","color",either(glob("AliceCoopAbout"),push(0xDCC996),either(glob("AliceCoopConfirm"),push(0xE4B766),glob("AliceCoopStatusColor"))))+discard(method("coop_status","setTextFormat",[var("coop_status_format")]))
dim=b""
for index,key in [(0,"AliceCoopCanJoin"),(1,"AliceCoopCanSync")]:
    opacity=either(glob(key),push(100),push(35))
    dim+=setmember("menu_btn_"+str(index)+".txt","_alpha",opacity)+setmember("menu_btn_"+str(index)+".bg.txt","_alpha",opacity)
refresh+=cond(glob("AliceCoopAbout")+op(0x12),cond(glob("AliceCoopConfirm")+op(0x12),cond(glob("AliceCoopHost")+op(0x12),dim)))
refresh+=cond(glob("AliceCoopAbout"),align_icons)
refresh_body=refresh
# The native menu is created during its entrance animation, before it owns
# keyboard focus. Release sibling screens at its native entrance (frame two),
# after profile selection, while keeping menu input locked until frame 22.
# These names/frames are verified against the stock movie. Hidden clips can
# retain Key listeners, so visibility alone does not prevent a second Enter.
def isolate_screens(only_interactive=True):
    title = var("_root.titleScreen")
    pressed = cond(eq(title+push("_currentframe")+op(0x4e),push(150)),
        discard(method("flash.external.ExternalInterface","call",[push("TitleOnStartPressed")])))
    cleanup = cond(title+push("_visible")+op(0x4e),pressed)+discard(method("Key","removeListener",[title]))
    # Keep the timeline parked without replaying its transition handlers.
    cleanup += discard(method("_root.titleScreen","stop"))
    cleanup += setmember("_root.titleScreen","_visible",push(False))
    cleanup += discard(method("Key","removeListener",[var("_root.profile.content")]))
    cleanup += setmember("_root.profile","_visible",push(False))
    return cond(glob("AliceCoopInteractive"),cleanup) if only_interactive else cleanup
stage_entrance=isolate_screens(False)
stage_enter=setg("AliceCoopInteractive",push(True))+isolate_screens()
stage_leave=setg("AliceCoopInteractive",push(False))
profile_enter=setmember("this","_visible",push(True))
title_enter=setmember("this","_visible",push(True))
# Add isolation before the existing refresh body; keep passive status updates
# available while the native entrance animation is playing.
refresh=fn("CoopRefresh",isolate_screens()+cond(glob("AliceCoopOpen"),refresh_body)+ret())
register=refresh+setg("AliceCoopMC",var("this"))+setg("AliceCoopReady",push(1))
register+=setg("AliceCoopViewportReady",push(0))+setg("AliceCoopOpen",push(False))+setg("AliceCoopConfirm",push(False))+setg("AliceCoopAbout",push(False))+setg("AliceCoopCommand",push(0))+stage_leave

parts={"EnterPrefix":enter,"BackPrefix":cond(glob("AliceCoopOpen"),back()),
    "IgnoreOtherShortcut":cond(glob("AliceCoopOpen"),ret()),"ChooseSubmenu":choose,
    "StageEntrance":stage_entrance,"StageEnter":stage_enter,"StageLeave":stage_leave,"ProfileEnter":profile_enter,"TitleEnter":title_enter,
    "AppendMultiplayer":append,"CreateInfo":create_info,"ClearInfo":clear_info,"Register":register}
output=Path(__file__).resolve().parents[1]/"include/CoopScaleformActions.inl"
lines=["// Generated by tools/Generate-Coop-Scaleform.py. Contains only our AVM1 additions."]
for name,data in parts.items():
    lines.append("inline constexpr uint8_t "+name+"[] = {")
    lines += ["    "+", ".join("0x%02X"%b for b in data[i:i+20])+"," for i in range(0,len(data),20)]
    lines.append("};")
output.write_text("\n".join(lines)+"\n")
print("Generated",output)
