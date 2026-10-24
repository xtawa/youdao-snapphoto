"""Real camera: profiles, filters, EXIF time and LAN export. Creates test media.
Run with SnapPhoto and stock camera in background. Never resets camera drivers.
"""
import argparse,datetime,hashlib,json,time,urllib.request,urllib.error
ap=argparse.ArgumentParser();ap.add_argument("--skip-share",action="store_true");args=ap.parse_args()
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
from probe_session import Probe,adb,ROOT
VERSION=json.loads((ROOT/'package.json').read_text(encoding='utf-8'))['version']
OUT=ROOT/'evidence/private'/VERSION;OUT.mkdir(parents=True,exist_ok=True)
evidence={'version':VERSION,'profiles':{},'filters':{}}
created=[]
def image_receipt(result,expected):
    created.append(result['path'])
    path=OUT/Path(result['path']).name;adb('pull',result['path'],path)
    with Image.open(path) as im:
        assert im.size==expected,(im.size,expected)
        exif=im.getexif().get_ifd(0x8769)
        date=exif[0x9003];offset=exif[0x9011];sub=exif[0x9291]
        dt=datetime.datetime.strptime(date,'%Y:%m:%d %H:%M:%S')
        assert dt.strftime('SNAP_%Y%m%d_%H%M%S_') in path.name
        actual=datetime.datetime.fromisoformat(dt.isoformat()+'.'+sub+offset)
        assert abs(actual.timestamp()*1000-result['capturedAtEpochMs'])<1.1
        assert actual.isoformat(timespec='milliseconds')==result['capturedAt']
        im.load()
    return path,{'sourcePath':result['path'],'size':list(expected),'bytes':path.stat().st_size,'exifTime':date+'.'+sub+offset,'processingMs':result.get('processingMs'),'saveMs':result.get('saveMs')}
with Probe() as p:
    caps=p.call('caps');p.call('orientation',value='landscape');p.call('captureMode',value='photo');p.call('open');p.wait_frame()
    for profile in caps['photoProfiles']:
        p.call('photoProfile',value=profile['id']);time.sleep(.2)
        before=time.monotonic();photo=p.photo()
        local,receipt=image_receipt(photo,(profile['width'],profile['height']))
        receipt['captureSeconds']=round(time.monotonic()-before,3)
        evidence['profiles'][profile['id']]=receipt;p.wait_frame();print('profile',profile['id'],receipt,flush=True)
    assert p.call('photoProfile',value='bogus',allow_error=True).get('error')
    p.call('photoProfile',value='wide')
    for effect in caps['filters']:
        p.call('filter',value=effect);photo=p.photo()
        local,receipt=image_receipt(photo,(3264,979));evidence['filters'][effect]=receipt
        if effect=='mono':
            with Image.open(local) as im:
                r,g,b=im.convert('RGB').split();assert max(ImageStat.Stat(ImageChops.difference(r,g)).mean)<2
        print('filter',effect,'ok',flush=True)
    p.call('filter',value='none');p.call('zoom',value=2)
    _,evidence['zoom2']=image_receipt(p.photo(),(3264,979));p.call('zoom',value=1)
    p.call('orientation',value='portrait')
    _,evidence['portrait']=image_receipt(p.photo(),(979,3264))
    square=next(profile for profile in caps['photoProfiles'] if profile['id']=='square')
    p.call('photoProfile',value='square');_,evidence['portraitSquare']=image_receipt(p.photo(),(square['width'],square['height']))
    p.call('orientation',value='landscape');p.call('videoProfile',value='compact');p.call('captureMode',value='video');time.sleep(.3)
    p.call('record');time.sleep(2);video=p.call('stopRecord');created.append(video['path'])
    details=json.loads(adb('shell','ffprobe -v error -show_entries stream=codec_name,width,height -of json '+video['path']))
    assert video.get('audio') and video.get('audioXruns')==0,video
    assert details['streams'][1]['codec_name']=='pcm_s16le',details
    assert details['streams'][0]['width']==640 and details['streams'][0]['height']==192,details
    assert not adb('shell','ffmpeg -v error -i '+video['path']+' -f null - 2>&1').strip();evidence['video']=details
    p.call('captureMode',value='photo');p.call('photoProfile',value='wide');photo=p.photo();local,_=image_receipt(photo,(3264,979))
    if not args.skip_share:
        share=p.call('share',paths=[photo['path'],video['path']]);url=share['url']
        opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
        html=opener.open(url,timeout=8).read();(OUT/'export-page.html').write_bytes(html)
        assert b'<article>' in html and '拍摄于'.encode() in html and b'metadata.json' in html
        meta=json.load(opener.open(url+'metadata.json',timeout=8))
        assert meta['files'][0]['name']==local.name and meta['files'][0].get('capturedAtLocal')
        assert '/userdisk/' not in json.dumps(meta)
        response=opener.open(url+'f/0',timeout=8);data=response.read()
        assert "filename*=UTF-8''"+local.name in response.headers['Content-Disposition']
        assert hashlib.sha256(data).digest()==hashlib.sha256(local.read_bytes()).digest()
        preview=opener.open(url+'p/0',timeout=8);assert preview.headers['Content-Type']=='image/jpeg';assert preview.read()==data
        for suffix,code in [('f/999',404),('f/0/../1',404),('f/0?x=1',404)]:
            try:opener.open(url+suffix,timeout=5);raise AssertionError('accepted '+suffix)
            except urllib.error.HTTPError as ex:assert ex.code==code
        try:opener.open(url.replace(url.split('/')[-2],'wrong-token'),timeout=5);raise AssertionError('unauthenticated')
        except urllib.error.HTTPError as ex:assert ex.code==403
        p.call('stopShare')
        try:opener.open(url,timeout=3);raise AssertionError('share did not stop')
        except urllib.error.URLError:pass
        evidence['share']={'html':True,'timeManifest':True,'originalFilename':True,'downloadExactBytes':True,'thumbnail':True,'unauthorized403':True,'invalidPath404':True,'stopClosed':True}
    else:evidence['share']={'tested':False,'reason':'User requested skipping LAN testing'}
    p.call('close');p.call('open');evidence['reopen']=bool(p.wait_frame().get('image'))
    evidence['preview']=p.call('frame');evidence['preview'].pop('image',None)
    p.call('close');evidence['cleanup']=p.call('delete',paths=list(dict.fromkeys(created)))
(ROOT/f'evidence/profiles-filters-time-{VERSION}.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf-8')
print('PASS: profiles, filters, EXIF time, portrait, video and camera reopen; share', 'SKIPPED by user' if args.skip_share else 'PASS')
