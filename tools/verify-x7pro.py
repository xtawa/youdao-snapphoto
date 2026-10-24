"""Explicit real-device smoke test. Creates photos/video; leaves media intact."""
from pathlib import Path
import subprocess,json,time,urllib.request,hashlib,threading,queue
root=Path(__file__).resolve().parent.parent
def adb(*args):
    return subprocess.check_output(['adb','-d',*args]).decode(errors='replace')
adb('push',str(root/'native/snapphoto-probe'),'/tmp/snapphoto-probe')
adb('shell','chmod +x /tmp/snapphoto-probe')
p=subprocess.Popen(['adb','-d','shell','/tmp/snapphoto-probe'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
lines=queue.Queue()
def reader():
    for line in p.stdout:lines.put(line)
threading.Thread(target=reader,daemon=True).start()
def call(op,**kw):
    p.stdin.write(json.dumps(dict(op=op,**kw))+'\n');p.stdin.flush()
    end=time.monotonic()+15
    while time.monotonic()<end:
        line=lines.get(timeout=max(.01,end-time.monotonic()))
        try:r=json.loads(line)
        except ValueError:continue
        if 'op' in r:continue
        if isinstance(r,dict) and 'error' in r:raise RuntimeError(op+': '+r['error'])
        return r
    raise RuntimeError('Native test timed out: '+op)
evidence={}
try:
    evidence['capabilities']=call('caps');call('open')
    for _ in range(30):
        evidence['preview']=call('frame')
        if evidence['preview'].get('image'):break
        time.sleep(.2)
    else:raise AssertionError('No preview within six seconds')
    call('zoom',value=2);call('filter',value='mono');time.sleep(.2)
    mono=call('photo');call('zoom',value=1);call('filter',value='none');time.sleep(.2)
    photo=call('photo');evidence['photo']=photo
    call('record');time.sleep(3);video=call('stopRecord');evidence['video']=video;evidence['steadyPreview']=call('frame')
    files=call('list');assert all(any(x['path']==f['path'] for x in files) for f in [mono,photo,video])
    share=call('share',paths=[photo['path'],video['path']]);url=share['url']
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    assert b'SnapPhoto' in opener.open(url,timeout=8).read()
    for index,item in enumerate([photo,video]):
        data=opener.open(url+'f/'+str(index),timeout=8).read()
        local=root/'evidence/private'/Path(item['path']).name;local.parent.mkdir(parents=True,exist_ok=True)
        adb('pull',item['path'],str(local));assert hashlib.sha256(data).digest()==hashlib.sha256(local.read_bytes()).digest()
    try:opener.open(url.replace(url.split('/')[-2],'incorrect'),timeout=8);raise AssertionError('Unauthenticated request accepted')
    except urllib.error.HTTPError as e:assert e.code==403
    evidence['share']={'html':True,'downloads_sha256_match':True,'unauthorized_status':403}
    call('stopShare');call('close');call('open')
    for _ in range(25):
        if call('frame').get('image'):break
        time.sleep(.2)
    else:raise AssertionError('Camera did not reopen within five seconds')
    call('close')
    evidence['reopen']=True
    from PIL import Image
    image=Image.open(root/'evidence/private'/Path(photo['path']).name);assert image.size==(3264,979);image.verify()
    evidence['jpeg_decode']={'width':3264,'height':979}
    ff=adb('shell','ffprobe -v error -show_entries stream=codec_name,width,height,nb_frames,r_frame_rate -show_entries format=duration -of json '+video['path'])
    evidence['video_probe']=json.loads(ff);assert evidence['video_probe']['streams'][0]['codec_name']=='mjpeg'
    assert float(evidence['video_probe']['format']['duration'])>1
    out=adb('shell','ffmpeg -v error -i '+video['path']+' -f null - 2>&1');assert not out.strip(),out
    evidence['video_full_decode']=True
finally:
    p.stdin.write('{"op":"quit"}\n');p.stdin.flush();p.wait(timeout=10)
(root/'evidence').mkdir(exist_ok=True)
(root/'evidence/native-smoke.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')
print('PASS: physical camera, JPEG decode, AVI full decode, gallery, authenticated LAN downloads, close/reopen')
