"""Real RK3562 inference and bounded photo jobs using synthetic fixtures only."""
import hashlib,json,math,time
from pathlib import Path
from PIL import Image
from probe_session import Probe,adb,ROOT
OUT=ROOT/'native/cache/npu-fixtures';OUT.mkdir(parents=True,exist_ok=True)
MODEL='/tmp/snapphoto-ffdnet-int8.rknn'
SOURCE='/userdisk/Pictures/SNAP_NPU_VALIDATION_SYNTHETIC.jpg'
report={'version':'0.9.1','fixture':'synthetic gradient, text, edges and sigma-12 noise','checks':{}}
def psnr(a,b):
    square=sum((x-y)**2 for x,y in zip(a,b))/len(a)
    return 99 if square==0 else 10*math.log10(255**2/square)
def enhance(p,mode):
    p.call('enhanceStart',path=SOURCE,mode=mode)
    progress=[];began=time.monotonic()
    while time.monotonic()-began<125:
        r=p.call('enhanceResult')
        if not r.get('pending'):return r,progress
        progress.append(r['progress']);time.sleep(.08)
    raise TimeoutError('NPU photo job')
before=adb('shell','sha256sum /userdisk/snapphoto/preferences.json').split()[0]
adb('push',ROOT/'assets/models/ffdnet-color-rk3562.rknn',MODEL)
adb('push',OUT/'noisy.jpg',SOURCE)
original=hashlib.sha256((OUT/'noisy.jpg').read_bytes()).hexdigest()
owned=[]
with Probe(model=MODEL) as p:
    try:
        caps=p.call('caps');assert caps['npuReady'] and caps['enhancement'] and not caps['hdr'];report['capabilities']=caps
        assert 'error' in p.call('enhanceStart',allow_error=True,path='/etc/passwd',mode='quality')
        assert 'error' in p.call('enhanceStart',allow_error=True,path=SOURCE,mode='invalid')
        result,progress=enhance(p,'quality');owned.append(result['path'])
        assert result['npuUsed'] and result['denoiseApplied'] and result['tiles']==6
        assert progress==sorted(progress) and progress[-1]>0
        adb('pull',result['path'],OUT/'enhanced.jpg')
        clean=Image.open(OUT/'clean.png').convert('RGB');noisy=Image.open(OUT/'noisy.jpg').convert('RGB');enhanced=Image.open(OUT/'enhanced.jpg').convert('RGB')
        assert enhanced.size==clean.size
        crop=(28,28,clean.width-28,clean.height-28)
        a=psnr(noisy.crop(crop).tobytes(),clean.crop(crop).tobytes());b=psnr(enhanced.crop(crop).tobytes(),clean.crop(crop).tobytes())
        assert b>a+1,(a,b)
        # Seams should not introduce jumps larger than local noisy-source edges.
        seam=[]
        for x in [208,416]:
            for im in [noisy,enhanced]:
                seam.append(sum(abs(im.getpixel((x,y))[c]-im.getpixel((x-1,y))[c]) for y in range(28,im.height-28) for c in range(3))/((im.height-56)*3))
        assert seam[1]<=seam[0]+1 and seam[3]<=seam[2]+1,seam
        report['quality']={'receipt':result,'progressSamples':len(progress),'noisyPsnr':a,'enhancedPsnr':b,'verticalSeamMAE':seam}
        p.call('enhanceStart',path=SOURCE,mode='quality');p.call('close')
        assert 'error' in p.call('enhanceResult',allow_error=True)
        report['checks']['cancelOnClose']=True
        assert adb('shell','sha256sum '+SOURCE).split()[0]==original
        report['checks']['originalUnchanged']=True
    finally:
        p.call('close')
        p.call('delete',paths=[SOURCE]+owned)
assert before==adb('shell','sha256sum /userdisk/snapphoto/preferences.json').split()[0]
report['checks']['preferencesUnchanged']=True
(ROOT/'evidence/npu-photo-0.9.1.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report['quality'],indent=2))
