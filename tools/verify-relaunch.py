"""Repeated real UI destruction/reload in the same firmware host process."""
import json,re,subprocess,time
from pathlib import Path
from probe_session import adb,ROOT
OUT=ROOT/'evidence/private/0.9.3';OUT.mkdir(parents=True,exist_ok=True)
LOG=adb('shell','readlink /proc/$(pidof miniapp)/fd/1').strip()
assert re.fullmatch(r'/userdata/applog/DictPen_[0-9_]+\.log',LOG)
def tap(x,y):subprocess.run(['python',str(ROOT.parent/'xiaoc-research/device.py'),'tap',str(x),str(y)],check=True,capture_output=True)
def capture(name):subprocess.run(['python',str(ROOT.parent/'xiaoc-research/device.py'),'capture',str(OUT/name)],check=True,capture_output=True)
def frame():return adb('shell','ls -1t /tmp/snapphoto/preview-*.jpg 2>/dev/null | head -1').strip()
host=adb('shell','pidof miniapp').strip();report={'version':'0.9.3','sameFirmwareHost':True,'cycles':[]}
adb('shell','if [ "$(cat /sys/class/backlight/backlight/brightness)" = 0 ]; then cat /tmp/snapphoto-wake-event > /dev/input/event0; fi')
for cycle in range(3):
    tap(42,42);time.sleep(2.6)
    offset=int(adb('shell','wc -c < '+LOG).strip())
    adb('shell','miniapp_cli start 8001953602710246');time.sleep(1.2)
    log=adb('shell',f'tail -c +{offset+1} '+LOG)
    assert '[snapphoto] launch' in log and '[snapphoto] app show' in log,log[-1000:]
    assert "could not load module filename 'photoAlbum'" not in log
    assert 'ReferenceError:' not in log and 'TypeError:' not in log
    first=frame();time.sleep(.25);second=frame();assert first and second and first!=second,(first,second)
    capture(f'camera-{cycle}.png')
    tap(894,240);time.sleep(.6);capture(f'gallery-{cycle}.png');tap(42,42)
    assert adb('shell','pidof miniapp').strip()==host
    report['cycles'].append({'number':cycle+1,'launchHook':True,'appShow':True,'freshPreviewFrames':True,'galleryOpened':True,'moduleImportErrors':False})
    print('relaunch',cycle+1,'PASS',flush=True)
(ROOT/'evidence/relaunch-0.9.3.json').write_text(json.dumps(report,indent=2)+'\n')
