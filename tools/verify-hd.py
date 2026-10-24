"""Live recording matrix; only session configuration and owned test media change."""
import argparse,fractions,json,subprocess,time
from pathlib import Path
from probe_session import Probe,adb,ROOT
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--version',default=json.loads((ROOT/'package.json').read_text())['version'])
args=parser.parse_args()
OUT=ROOT/'evidence/private'/args.version;OUT.mkdir(parents=True,exist_ok=True)
report={'version':args.version,'takes':[]}

def inspect(result,label,expected):
    assert result.get('written') and result['audio'] and result['audioXruns']==0,result
    local=OUT/(label+'.avi');adb('pull',result['path'],local)
    meta=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(local)]))
    video,audio=meta['streams'];assert [video['width'],video['height']]==expected,meta
    assert audio['codec_name']=='pcm_s16le' and audio['sample_rate']=='16000' and int(audio['nb_frames'])>0
    decoded=subprocess.run(['ffmpeg','-v','error','-i',str(local),'-f','null','-'],capture_output=True,text=True)
    assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
    diff=abs(float(video['duration'])-int(audio['nb_frames'])/16000)
    assert diff<.2,diff
    r={'label':label,'size':expected,'videoFrames':int(video['nb_frames']),'videoFps':float(fractions.Fraction(video['avg_frame_rate'])),
       'durationSeconds':float(video['duration']),'audioSamples':int(audio['nb_frames']),'avDurationDifferenceMs':diff*1000,
       'bytes':local.stat().st_size,'fullDecodePassed':True,'recordResult':result}
    deleted=p.call('delete',paths=[result['path']]);assert not deleted.get('failed'),deleted
    return r

if adb('shell','cat /sys/class/backlight/backlight/brightness').strip()=='0':
    adb('shell','cat /tmp/snapphoto-wake-event > /dev/input/event0');time.sleep(.3)
route=adb('shell','amixer -c 1 sget "Capture MIC Path"')
with Probe() as p:
    caps=p.call('caps');assert [v['id'] for v in caps['videoProfiles']]==['compact','hd','fhd']
    p.call('open');p.wait_frame();p.call('captureMode',value='video')
    for profile in caps['videoProfiles']:
        for orientation in ['landscape','portrait']:
            if adb('shell','cat /sys/class/backlight/backlight/brightness').strip()=='0':
                adb('shell','cat /tmp/snapphoto-wake-event > /dev/input/event0');time.sleep(.3)
            p.call('orientation',value=orientation,rotation=270 if orientation=='portrait' else 0);p.call('videoProfile',value=profile['id']);time.sleep(.15)
            p.call('record');assert p.call('videoProfile',value='compact',allow_error=True).get('error')
            assert p.call('orientation',value='landscape',allow_error=True).get('error')
            began=time.monotonic();last=None
            while time.monotonic()-began<4:
                last=p.call('frame');assert not last.get('recordError'),last;time.sleep(.06)
            result=p.call('stopRecord')
            expected=[profile['width'],profile['height']] if orientation=='landscape' else [profile['portraitWidth'],profile['portraitHeight']]
            r=inspect(result,profile['id']+'-'+orientation,expected);r['nativeFrame']=last
            r['clockKHz']=adb('shell','cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq').strip()
            assert r['videoFps']>=(15 if profile['id']=='fhd' else 23),r
            report['takes'].append(r);print(r['label'],'fps',round(r['videoFps'],2),'drops',result['droppedFrames'],flush=True)
    # Same production limiter, lower threshold exposed only by the CLI probe.
    p.call('orientation',value='landscape');p.call('videoProfile',value='hd');p.call('record',maxBytes=1048576)
    for _ in range(80):
        frame=p.call('frame')
        if frame.get('recordLimit'):break
        time.sleep(.05)
    assert frame.get('recordLimit'),frame
    stopped=p.call('stopRecord');assert stopped.get('stopReason')=='file-limit',stopped
    report['fileLimit']=inspect(stopped,'limit-finalized',[1280,720]);assert report['fileLimit']['bytes']<=1048576
    # Background uses close to flush the independent worker and audio together.
    existing={m['path'] for m in p.call('list')};p.call('record');time.sleep(2);p.call('close')
    created={m['path'] for m in p.call('list')}-existing;assert len(created)==1
    path=created.pop();report['background']=inspect({'path':path,'written':True,'audio':True,'audioXruns':0},'background',[1280,720])
    p.call('open');report['reopen']=bool(p.wait_frame().get('image'))
report['microphoneReleased']=adb('shell','cat /proc/asound/card1/pcm0c/sub0/status').strip()=='closed'
report['microphoneRouteRestored']=route==adb('shell','amixer -c 1 sget "Capture MIC Path"')
assert report['microphoneReleased'] and report['microphoneRouteRestored']
(ROOT/f'evidence/hd-validation-{args.version}.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('PASS HD matrix, file limit, background finalization, microphone ownership and reopen',flush=True)
