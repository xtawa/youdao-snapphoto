"""Comparable live pipelines. Keep pen awake; never changes CPU frequency.
Run with every camera app in background. Only test-owned paths are recorded.
"""
import json,time
from pathlib import Path
from probe_session import Probe,adb,ROOT

result={'conditions':'Same pen, awake display, no clock/governor changes','versions':{}}
baseline=ROOT.parent.parent/'backups/snapphoto-0.5.0-performance/native/snapphoto-probe'
for version,binary in [('0.5.0',baseline),('0.6.0',None)]:
    report={}
    with Probe(binary) as p:
        p.call('caps');began=time.monotonic();p.call('open');p.wait_frame()
        report['firstFrameSeconds']=time.monotonic()-began
        for mode,orientation in [('wide','landscape'),('portrait','portrait'),('video','landscape')]:
            p.call('orientation',value=orientation);p.call('captureMode',value='video' if mode=='video' else 'photo')
            started=time.monotonic();seen=set()
            while time.monotonic()-started<3:
                frame=p.call('frame')
                if frame.get('image'):seen.add(frame['image'])
                time.sleep(.033)
            elapsed=time.monotonic()-started
            frame.pop('image',None)
            report[mode]={'uniquePolledFps':len(seen)/elapsed,'native':frame,
                          'clockKHz':adb('shell','cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq').strip(),
                          'brightness':adb('shell','cat /sys/class/backlight/backlight/brightness').strip()}
        p.call('captureMode',value='photo');started=time.monotonic();photo=p.photo()
        report['photo']={k:v for k,v in photo.items() if k!='path'};report['photo']['totalSeconds']=time.monotonic()-started
        adb('shell',"rm -- '"+photo['path']+"'") # This invocation created it.
        if version=='0.6.0':
            p.call('focusStart');until=time.monotonic()+4
            while time.monotonic()<until:
                focus=p.call('focusResult')
                if not focus.get('pending'):break
                time.sleep(.05)
            assert 'settled' in focus,focus;report['continuousFocus']=focus
            p.call('focusStart');started=time.monotonic();p.call('close')
            report['cancelFocusCloseSeconds']=time.monotonic()-started
            p.call('open');report['reopenAfterCancellation']=bool(p.wait_frame().get('image'))
    result['versions'][version]=report
    print(version,json.dumps(report),flush=True)
(ROOT/'evidence/performance-0.6.0.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
