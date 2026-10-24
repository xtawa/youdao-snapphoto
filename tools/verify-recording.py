"""Live audio/video, repeat takes, microphone ownership and close finalization.
No network export; creates only test media and never changes mixer controls.
"""
import array,json,math,subprocess,time
from pathlib import Path
from probe_session import Probe,adb,ROOT

out=ROOT/'evidence/private/0.6.0';out.mkdir(parents=True,exist_ok=True)
report={'version':'0.6.0','takes':[]}
before=adb('shell','amixer -c 1 sget "Capture MIC Path"')
def receipt(path,label):
    local=out/(label+'.avi');adb('pull',path,local)
    meta=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(local)]))
    video,audio=meta['streams'];assert video['codec_name']=='mjpeg' and audio['codec_name']=='pcm_s16le'
    assert audio['sample_rate']=='16000' and audio['channels']==1
    decoded=subprocess.run(['ffmpeg','-v','error','-i',str(local),'-f','null','-'],capture_output=True,text=True)
    assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
    raw=subprocess.check_output(['ffmpeg','-v','error','-i',str(local),'-vn','-f','s16le','-']);samples=array.array('h',raw)
    assert len(samples)>10000
    duration=len(samples)/16000;diff=abs(float(video['duration'])-duration)
    assert diff<.16,diff
    result={'label':label,'size':local.stat().st_size,'videoSize':[video['width'],video['height']],
            'videoFrames':int(video['nb_frames']),'videoFps':int(video['nb_frames'])/float(video['duration']),
            'audioSamples':len(samples),'durationSeconds':duration,'avDurationDifferenceMs':diff*1000,
            'audioPeak':max(map(abs,samples)),'audioRms':math.sqrt(sum(x*x for x in samples)/len(samples)),
            'fullDecodePassed':True}
    assert result['audioPeak']<=30000
    adb('shell',"rm -- '"+path+"'") # This test created this exact path.
    return result

with Probe() as p:
    p.call('caps');p.call('captureMode',value='video');p.call('open');p.wait_frame()
    for orientation in ['landscape','portrait','landscape']:
        p.call('orientation',value=orientation);time.sleep(.15);p.call('record');time.sleep(2)
        result=p.call('stopRecord');assert result['audio'] and result['audioXruns']==0 and result['audioLastError']==0,result
        report['takes'].append(receipt(result['path'],'repeat-'+str(len(report['takes']))))
    files={f['path'] for f in p.call('list')};p.call('record');time.sleep(2)
    started=time.monotonic();p.call('close');report['closeFinalizeSeconds']=time.monotonic()-started
    created={f['path'] for f in p.call('list')}-files;assert len(created)==1
    report['backgroundTake']=receipt(created.pop(),'background-finalized')
    report['micAfterClose']=adb('shell','cat /proc/asound/card1/pcm0c/sub0/status').strip();assert report['micAfterClose']=='closed'
    # Own a short PCM probe to verify busy-device failure, then wait for its exit.
    busy=subprocess.Popen(['adb','-d','shell','arecord -q -D softvol_cap -f S16_LE -c 2 -r 16000 -d 3 -t raw /tmp/snapphoto-busy.raw'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    try:
        time.sleep(.5);p.call('open');p.wait_frame();failed=p.call('record',allow_error=True)
        assert failed.get('error'),failed;report['busyMicrophoneRejected']=True
    finally:busy.communicate(timeout=10)
    p.call('record');time.sleep(1);result=p.call('stopRecord');assert result['audioXruns']==0
    report['recoveredTake']=receipt(result['path'],'after-busy')
    p.call('close');p.call('open');report['cameraReopened']=bool(p.wait_frame().get('image'))
report['micAfterAll']=adb('shell','cat /proc/asound/card1/pcm0c/sub0/status').strip()
report['microphoneRouteRestored']=before==adb('shell','amixer -c 1 sget "Capture MIC Path"')
assert report['microphoneRouteRestored'] and report['micAfterAll']=='closed'
(ROOT/'evidence/recording-stability-0.6.0.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report),flush=True)
