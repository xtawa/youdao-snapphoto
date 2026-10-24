"""Bounded 8MP synthetic JPEG processing and passive AF regression on X7 Pro."""
from pathlib import Path
import json,time,re
from PIL import Image
from probe_session import Probe,adb,ROOT
out=ROOT/'native/cache/npu-fixtures'
source='/userdisk/Pictures/SNAP_NPU_FULL_VALIDATION.jpg'
adb('push',out/'full.jpg',source)
owned=[source];report={'sourceSize':[3264,2448],'condition':'Display awake; original governor unchanged'}
adb('shell','if [ "$(cat /sys/class/backlight/backlight/brightness)" = 0 ]; then cat /tmp/snapphoto-wake-event > /dev/input/event0; fi')
with Probe(model='/tmp/snapphoto-ffdnet-int8.rknn') as p:
    try:
        p.call('caps');p.call('open');p.wait_frame();time.sleep(.7)
        p.call('focusStart')
        while True:
            focus=p.call('focusResult')
            if not focus.get('pending'):break
            time.sleep(.05)
        report['previewFocus']=focus
        pid=adb('shell',"pgrep -f '^/tmp/snapphoto-validation$'").strip();assert pid.isdigit(),pid
        p.call('enhanceStart',path=source,mode='quality');samples=[];rss=[];start=time.monotonic()
        while time.monotonic()-start<125:
            result=p.call('enhanceResult')
            if not result.get('pending'):break
            samples.append(result['progress']);status=adb('shell','cat /proc/'+pid+'/status')
            match=re.search(r'VmRSS:\s*(\d+)',status)
            if match:rss.append(int(match[1]))
            adb('shell','if [ "$(cat /sys/class/backlight/backlight/brightness)" = 0 ]; then cat /tmp/snapphoto-wake-event > /dev/input/event0; fi')
            time.sleep(.15)
        assert result.get('npuUsed') and result['tiles']==192,result
        owned.append(result['path']);adb('pull',result['path'],out/'full-enhanced.jpg')
        with Image.open(out/'full-enhanced.jpg') as image:
            image.load();assert image.size==(3264,2448);assert image.getexif().get_ifd(34665).get(36867)
        report.update(receipt=result,peakRSSKiB=max(rss),progressSamples=len(samples),fullJpegDecode=True,
                      previewWhileEnhancing=p.call('frame').get('previewFps'))
        # Both photo paths must either produce a stable fresh frame or report
        # an explicit focus error; they must not hang the capture pipeline.
        p.call('photoStart')
        while True:
            photo=p.call('photoResult',allow_error=True)
            if not photo.get('pending'):break
            time.sleep(.1)
        if photo.get('path'):owned.append(photo['path'])
        else:assert 'stable' in photo.get('error',''),photo
        report['freshPhoto']=photo
        p.call('captureMode',value='video');p.call('videoProfile',value='hd');p.call('record');time.sleep(2)
        video=p.call('stopRecord');owned.append(video['path']);report['video']=video
        adb('pull',video['path'],out/'regression.avi')
    finally:
        p.call('close');p.call('delete',paths=owned)
        (ROOT/'evidence/npu-full-photo-0.9.1.json').write_text(json.dumps(report,indent=2)+'\n')
(ROOT/'evidence/npu-full-photo-0.9.1.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
