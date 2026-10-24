"""Real ZSL and independent worker tests; only owned synthetic/media files."""
import json,time,re,subprocess
from pathlib import Path
from probe_session import Probe,adb,ROOT
MODEL='/tmp/snapphoto-ffdnet-int8.rknn';WORKER='/tmp/snapphoto-background-worker'
SOURCE='/userdisk/Pictures/SNAP_NPU_BACKGROUND_VALIDATION.jpg'
adb('push',ROOT/'native/snapphoto-probe',WORKER);adb('shell','chmod 700 '+WORKER)
adb('push',ROOT/'native/cache/npu-fixtures/full.jpg',SOURCE)
report={'version':'0.9.2','photos':[]};owned=[SOURCE];job_ids=[]
def wake():adb('shell','if [ "$(cat /sys/class/backlight/backlight/brightness)" = 0 ]; then cat /tmp/snapphoto-wake-event > /dev/input/event0; fi')
def find(status,job_id):return next(job for job in status['jobs'] if job['id']==job_id)
def finish(p,job_id):
    start=time.monotonic()
    while time.monotonic()-start<125:
        status=p.call('queueStatus');job=find(status,job_id)
        if job['state'] in ['done','error']:return job
        wake();time.sleep(.2)
    raise TimeoutError('background job')
wake()
try:
    with Probe(model=MODEL,worker=WORKER) as p:
        caps=p.call('caps');assert caps['zeroShutterLag'] and caps['backgroundEnhancement']
        p.call('night',mode='auto',priority='quality');p.call('photoProfile',value='cinema');p.call('open');p.wait_frame();time.sleep(.3)
        for i in range(3):
            began=time.monotonic();photo=p.photo();owned.append(photo['path']);report['photos'].append(dict(photo,roundTripMs=(time.monotonic()-began)*1000))
            assert photo['selectionMs']<150
        start=time.monotonic();job=p.call('queueEnhance',path=SOURCE,mode='quality',keepAlive=True)
        report['enqueueMs']=(time.monotonic()-start)*1000;job_ids.append(job['jobId']);assert job['queued'] and report['enqueueMs']<500
        for i in range(2):
            began=time.monotonic();photo=p.photo();owned.append(photo['path']);report['photos'].append(dict(photo,roundTripMs=(time.monotonic()-began)*1000,backgroundWorker=True))
        state=find(p.call('queueStatus'),job['jobId']);assert state['state']=='processing',state
    # The original UI/probe process has exited. The worker must still exist.
    report['workerAfterUIExit']=adb('shell',"pgrep -af '^/tmp/snapphoto-background-worker --queue-worker'").strip()
    assert report['workerAfterUIExit']
    with Probe(model=MODEL,worker=WORKER) as p:
        done=finish(p,job['jobId']);assert done['state']=='done',done
        assert done['result']['npuUsed'] and done['result']['tiles']==192;owned.append(done['result']['path']);report['completedAfterUIExit']=done
        # A keepAlive=false job cannot run without a foreground lease.
        paused=p.call('queueEnhance',path=SOURCE,mode='auto',keepAlive=False);job_ids.append(paused['jobId']);time.sleep(.5)
        assert find(p.call('queueStatus'),paused['jobId'])['state']=='pending'
        p.call('open');p.wait_frame();p.call('queueResume');time.sleep(.3);p.call('close');time.sleep(.3)
        assert find(p.call('queueStatus'),paused['jobId'])['state']=='pending'
        report['pauseOnExit']=True
        p.call('open');p.wait_frame();p.call('queueResume');resumed=finish(p,paused['jobId']);assert resumed['state']=='done',resumed
        owned.append(resumed['result']['path']);report['resumed']=resumed
        p.call('close');p.call('delete',paths=owned);owned=[]
finally:
    if owned:
        with Probe(model=MODEL,worker=WORKER) as p:p.call('delete',paths=owned)
    for job_id in job_ids:
        assert re.fullmatch(r'job-[0-9-]+\.json',job_id)
        adb('shell','rm -f /userdisk/snapphoto/jobs/'+job_id)
(ROOT/'evidence/background-zsl-0.9.2.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'photosMs':[round(p['roundTripMs']) for p in report['photos']],
                  'selectionMs':[round(p['selectionMs'],2) for p in report['photos']],
                  'enqueueMs':report['enqueueMs'],'afterUIExit':True,'pauseAndResume':True},indent=2))
