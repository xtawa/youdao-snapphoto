"""Package the native adapter and bytecode compiled by the connected pen."""
from pathlib import Path
import subprocess,hashlib,json,zipfile,shutil
root=Path(__file__).resolve().parent.parent
def adb(*a):
    s=subprocess.check_output(['adb','-d',*a]).decode(errors='replace')
    if 'SyntaxError' in s or 'Exception' in s or 'login with' in s:raise RuntimeError(s)
    return s
assert 'uid=0(root)' in adb('shell','id')
source=root/'.falcon_/app.js'
if not source.exists():raise RuntimeError('Run npm run build first')
out=root/'dist';out.mkdir(exist_ok=True)
adb('push',str(root/'tools/jsfmc'),'/tmp/snap-jsfmc')
adb('push',str(source),'/tmp/snap-app.js');adb('shell','chmod +x /tmp/snap-jsfmc')
adb('shell','LD_LIBRARY_PATH=/oem/YoudaoDictPen/output/libs:/usr/lib:/lib /tmp/snap-jsfmc -o /tmp/snap-app.js.bin -n app.js /tmp/snap-app.js')
adb('pull','/tmp/snap-app.js.bin',str(out/'app.js.bin'))
lib=root/'native/libjsapi_snapphoto.so';name='libs/arm64/libjsapi_snapphoto_'+hashlib.sha256(lib.read_bytes()).hexdigest()[:10]+'.so'
files={'app.js.bin':(out/'app.js.bin').read_bytes(),name:lib.read_bytes()}
model_root=root/'assets/models'
model_meta=json.loads((model_root/'ffdnet.json').read_text())
model_bytes=(model_root/'ffdnet-color-rk3562.rknn').read_bytes()
assert hashlib.sha256(model_bytes).hexdigest()==model_meta['sha256'], 'Bundled model hash mismatch'
for asset in ['ffdnet-color-rk3562.rknn','ffdnet.json','FFDNet-LICENSE.txt']:
    files['assets/models/'+asset]=(model_root/asset).read_bytes()
files['assets/bin/snapphoto-worker']=(root/'native/snapphoto-probe').read_bytes()
shutil.copy2(root/'app_icon.png',out/'app_icon.png');files['app_icon.png']=(out/'app_icon.png').read_bytes()
version=json.loads((root/'package.json').read_text())['version']
manifest={'appName':'SnapPhoto','version':version,'appid':'8001953602710246','icon':'app_icon.png','quickjs':{'version':'20200705','bigNum':False},'props':{'supportUnInstall':True,'addDesktop':{'y02_1_platform':True}},'cert':{n:{'size':len(b),'md5':hashlib.md5(b).hexdigest()} for n,b in files.items()}}
files['manifest.json']=json.dumps(manifest,ensure_ascii=False).encode()
package=out/('SnapPhoto-X7Pro-'+version+'.amr')
with zipfile.ZipFile(package,'w',zipfile.ZIP_DEFLATED) as z:
    for n,b in files.items():z.writestr(n,b)
with zipfile.ZipFile(package) as z:
    assert z.testzip() is None
    for n,c in manifest['cert'].items():assert hashlib.md5(z.read(n)).hexdigest()==c['md5']
(out/'SHA256SUMS.txt').write_text(hashlib.sha256(package.read_bytes()).hexdigest()+'  '+package.name+'\n')
print(package,package.stat().st_size)
