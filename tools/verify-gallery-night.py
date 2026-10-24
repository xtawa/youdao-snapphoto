"""Owned test media only. No Wi-Fi/settings, firmware, USB or boot changes."""
import io,json,subprocess,time,urllib.request,urllib.error,wave
from pathlib import Path
from PIL import Image,ImageStat
from probe_session import Probe,adb,ROOT
VERSION=json.loads((ROOT/'package.json').read_text(encoding='utf-8'))['version']
OUT=ROOT/'evidence/private'/VERSION;OUT.mkdir(parents=True,exist_ok=True)
report={'version':VERSION,'takes':[],'photos':[]}
def wake():
    if adb('shell','cat /sys/class/backlight/backlight/brightness').strip()=='0':
        adb('shell','cat /tmp/snapphoto-wake-event > /dev/input/event0');time.sleep(.5)
def pull(path,name):
    local=OUT/name;adb('pull',path,local);return local
def thumbnail(p,path):
    r=p.call('thumbnailStart',path=path)
    if not r.get('pending'):return r
    for _ in range(130):
        r=p.call('thumbnailResult');
        if not r.get('pending'):return r
        time.sleep(.1)
    raise TimeoutError('thumbnail')
def save():
    (ROOT/f'evidence/gallery-night-{VERSION}.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
def video_meta(result,name):
    local=pull(result['path'],name+'.avi')
    meta=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(local)]))
    v=next(s for s in meta['streams'] if s['codec_type']=='video');a=next(s for s in meta['streams'] if s['codec_type']=='audio')
    decoded=subprocess.run(['ffmpeg','-v','error','-i',str(local),'-f','null','-'],capture_output=True)
    assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
    assert a['codec_name']=='pcm_s16le' and int(a['sample_rate'])==16000
    from fractions import Fraction
    return {'size':[v['width'],v['height']],'fps':float(Fraction(v['avg_frame_rate'])),'fullDecode':True,'audioSamples':int(a['nb_frames']),'audioXruns':result['audioXruns']}
wake()
with Probe() as p:
    p.call('open');p.wait_frame();p.call('photoProfile',value='sixteen-nine')
    for mode,priority in [('off','speed'),('auto','speed'),('auto','quality')]:
        wake();p.call('night',mode=mode,priority=priority);time.sleep(3)
        frame=p.call('frame');path=p.photo()['path'];local=pull(path,'night-'+mode+'-'+priority+'.JPG')
        mean=ImageStat.Stat(Image.open(local).convert('L')).mean[0]
        report['photos'].append({'mode':mode,'priority':priority,'path':path,'mean':mean,'frame':frame});save()
        print('photo',mode,priority,'mean',round(mean,2),flush=True)
    p.call('night',mode='off',priority='speed')
    for angle in [0,90,180,270]:
        wake();p.call('orientation',value='portrait' if angle%180 else 'landscape',rotation=angle)
        time.sleep(.2);result=p.photo();local=pull(result['path'],'rotation-'+str(angle)+'.JPG')
        expected=(1836,3264) if angle%180 else (3264,1836)
        assert Image.open(local).size==expected
        report.setdefault('rotationPhotos',[]).append({'angle':angle,'size':list(expected),'path':result['path']});save()
    p.call('captureMode',value='video')
    for profile,angle,mode,priority in [('hd',0,'off','speed'),('hd',0,'auto','speed'),('hd',0,'auto','quality'),('hd',90,'auto','speed'),('fhd',0,'auto','speed')]:
        wake();p.call('orientation',value='portrait' if angle%180 else 'landscape',rotation=angle);p.call('videoProfile',value=profile);p.call('night',mode=mode,priority=priority);time.sleep(1)
        p.call('record');time.sleep(4);frame=p.call('frame');result=p.call('stopRecord')
        name=f'{profile}-{angle}-{mode}-{priority}';info=video_meta(result,name)
        info.update({'profile':profile,'angle':angle,'nightMode':mode,'priority':priority,'native':result,'frame':frame})
        report['takes'].append(info);save();print('video',name,round(info['fps'],2),'fps',flush=True)
    p.call('close')
    image=report['photos'][0]['path'];video=report['takes'][0]['native']['path']
    t=thumbnail(p,image);vt=thumbnail(p,video);photo_thumb=Image.open(pull(t['path'],'image-thumb.jpg'))
    assert photo_thumb.size==(160,160) or photo_thumb.width==240 # Scaled JPEG or bounded decoder fallback.
    assert Image.open(pull(vt['path'],'video-thumb.jpg')).width==240
    report['thumbnails']={'photo':True,'video':True,'photoSize':list(photo_thumb.size),'photoFastPath':photo_thumb.size==(160,160)}
    p.call('play',path=video,orientation='landscape');time.sleep(1.2);frame1=p.call('playFrame');time.sleep(.5);frame2=p.call('playFrame')
    assert frame1['playing'] and frame2['image']!=frame1['image']
    report['playback']={'framesAdvance':True,'audioDevice':adb('shell','cat /proc/asound/card0/pcm0p/sub0/status 2>/dev/null').strip()}
    p.call('stopPlay');save()
    tag=VERSION.replace('.','');copy=f'/userdisk/Pictures/SNAP_DELETE_CHECK_{tag}.jpg';upper=copy[:-4]+'.JPG';link=f'/userdisk/Pictures/SNAP_LINK_CHECK_{tag}.JPG'
    assert adb('shell',f'test ! -e {copy} && test ! -e {upper} && test ! -e {link}; echo $?').strip()=='0'
    adb('shell',f'cp {image} {copy}')
    rejected=p.call('delete',paths=[copy,'/etc/hosts'],allow_error=True);assert rejected.get('error')
    assert adb('shell',f'test -f {copy}; echo $?').strip()=='0'
    publication=p.call('publish',path=copy);assert publication['path']==upper
    adb('shell',f'ln -s /etc/hosts {link}')
    assert p.call('delete',paths=[link],allow_error=True).get('error')
    adb('shell',f'rm -- {link}')
    deleted=p.call('delete',paths=[upper]);assert deleted['deleted']==[upper] and not deleted['failed']
    report['delete']={'mixedInvalidBatchPreservedValidFile':True,'symlinkRejected':True,'ownedMediaDeleted':True,'legacyJpegPublished':True};save()
    share=p.call('share',paths=[video,image],loopback=True)
    from urllib.parse import urlsplit
    remote=urlsplit(share['url']);localport=adb('forward','tcp:0','tcp:'+str(remote.port)).strip();base='http://127.0.0.1:'+localport+remote.path
    try:
        def get(suffix,headers=None):
            req=urllib.request.Request(base+suffix,headers=headers or {})
            with urllib.request.urlopen(req,timeout=10) as r:return r.status,r.read(),dict(r.headers)
        _,html,_=get('');assert b'playAvi' in html
        _,body,_=get('v/0');meta=json.loads(body);assert meta['frames']>0
        _,jpg,_=get('j/0/0');Image.open(io.BytesIO(jpg)).verify()
        status,wav,headers=get('a/0',{'Range':'bytes=0-64043'});assert status==206
        pcm=wave.open(io.BytesIO(wav));assert pcm.getnchannels()==1 and pcm.getframerate()==16000
        assert len(pcm.readframes(16000))==32000
        status,part,_=get('a/0',{'Range':'bytes=44-1043'});assert status==206 and len(part)==1000
        try:get('a/0',{'Range':'bytes=99999999999-'})
        except urllib.error.HTTPError as e:assert e.code==416
        else:raise AssertionError('Invalid range accepted')
        report['usbForwardExport']={'html':True,'aviMetadata':meta,'jpegFrame':True,'wavAudio':True,'audioRange':True,'invalidRangeRejected':True,'wifiTested':False}
    finally:adb('forward','--remove','tcp:'+localport);p.call('stopShare')
    save()
print('Validation completed',flush=True)
