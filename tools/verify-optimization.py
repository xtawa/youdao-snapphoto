"""Controlled camera A/B checks. Only media created by this run is removed."""
from pathlib import Path
import argparse,hashlib,json,subprocess,time
from PIL import Image
from probe_session import Probe,adb,ROOT
parser=argparse.ArgumentParser()
parser.add_argument('--baseline',type=Path,required=True)
parser.add_argument('--version',default='0.9.0')
args=parser.parse_args()
OUT=ROOT/'evidence/private'/args.version;OUT.mkdir(parents=True,exist_ok=True)
report={'version':args.version,'condition':'Same device/scene; awake; no clock/governor/mixer changes','runs':{}}
def save():
    (ROOT/f'evidence/optimization-{args.version}.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
def wake():
    adb('shell','if [ "$(cat /sys/class/backlight/backlight/brightness)" = 0 ]; then cat /tmp/snapphoto-wake-event > /dev/input/event0; fi')
def awake():
    brightness=int(adb('shell','cat /sys/class/backlight/backlight/brightness').strip())
    assert brightness>0,'Display slept: reject this timing sample'
    return {'brightness':brightness,'cpuKHz':int(adb('shell','cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq').strip())}
def focus(p):
    p.call('focusStart');until=time.monotonic()+4
    while time.monotonic()<until:
        result=p.call('focusResult')
        if not result.get('pending'):return result
        time.sleep(.025)
    raise TimeoutError('focus observation')
for label,binary in [('baseline',args.baseline),('optimized',ROOT/'native/snapphoto-probe')]:
    wake();owned=[];run={'binarySha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'preview':{},'photos':[]}
    report['runs'][label]=run
    with Probe(binary) as p:
        try:
            p.call('caps');p.call('night',mode='off',priority='speed');start=time.monotonic();p.call('open');p.wait_frame()
            run['firstFrameMs']=(time.monotonic()-start)*1000
            for name,mode,orientation in [('photo','photo','landscape'),('portrait','photo','portrait'),('hd-video','video','landscape')]:
                wake();p.call('orientation',value=orientation,rotation=270 if orientation=='portrait' else 0)
                p.call('captureMode',value=mode);p.call('videoProfile',value='hd');time.sleep(.4)
                seen=set();start=time.monotonic()
                while time.monotonic()-start<2.5:
                    frame=p.call('frame')
                    if frame.get('image'):seen.add(frame['image'])
                    time.sleep(.033)
                frame.pop('image',None)
                run['preview'][name]={'uniquePolledFps':len(seen)/(time.monotonic()-start),'frame':frame,
                    'clockKHz':adb('shell','cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq').strip()}
            p.call('captureMode',value='photo');p.call('orientation',value='landscape',rotation=0);p.call('photoProfile',value='sixteen-nine')
            for i in range(2):
                conditions=awake()
                start=time.monotonic();af=focus(p);photo=p.photo();elapsed=(time.monotonic()-start)*1000
                after=awake()
                owned.append(photo['path']);local=OUT/(label+'-'+str(i)+'.JPG');adb('pull',photo['path'],local)
                with Image.open(local) as image:image.load();assert image.size==(3264,1836)
                run['photos'].append({'focus':af,'photo':photo,'totalMs':elapsed,'conditionsBefore':conditions,'conditionsAfter':after})
                assert photo.get('candidateFrames',1) in ((1,3) if label=='optimized' else (1,))
            if label=='optimized':
                p.call('photoStart',fast=True)
                while True:
                    fast=p.call('photoResult')
                    if not fast.get('pending'):break
                    time.sleep(.03)
                owned.append(fast['path']);assert fast['candidateFrames']==1;run['fastBurst']=fast
            run['focusCacheCheck']=[focus(p),focus(p)]
            p.call('captureMode',value='video');p.call('videoProfile',value='hd');wake();p.call('record');time.sleep(3)
            video=p.call('stopRecord');run['videoConditions']=awake();owned.append(video['path']);local=OUT/(label+'.avi');adb('pull',video['path'],local)
            streams=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(local)]))['streams']
            assert any(s['codec_type']=='audio' and s['codec_name']=='pcm_s16le' for s in streams)
            decode=subprocess.run(['ffmpeg','-v','error','-i',str(local),'-f','null','-'],capture_output=True)
            assert decode.returncode==0 and not decode.stderr,decode.stderr
            run['video']={'receipt':video,'streams':streams,'fullDecode':True};assert video['audioXruns']==0
            p.call('close');p.call('open');run['reopen']=bool(p.wait_frame().get('image'))
            save()
        finally:
            p.call('close')
            if owned:run['cleanup']=p.call('delete',paths=owned)
            save()
    print(label,'PASS',json.dumps({'preview':{k:round(v['uniquePolledFps'],2) for k,v in run['preview'].items()},'photoMs':[round(v['totalMs']) for v in run['photos']],'cached':[v['focus'].get('cached') for v in run['photos']]}),flush=True)
print('A/B camera, AF observation, still images, fast burst, audio/video decode and reopen passed.')
