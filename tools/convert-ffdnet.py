"""Reproduce the bundled FFDNet core for RK3562, with public synthetic fixtures.

Original architecture/weights: Kai Zhang / cszn/KAIR, MIT.
The NPU handles the 12 learned convolution layers. Native code performs the
exact 2x pixel unshuffle/shuffle, halo cropping and conservative blending.
No device image or firmware binary is uploaded to the conversion runner.
"""
import hashlib,json,math,urllib.request
from pathlib import Path
import numpy as np
import torch
from torch import nn
from rknn.api import RKNN

OUT=Path('build/ffdnet-rk3562');OUT.mkdir(parents=True,exist_ok=True)
WEIGHT_URL='https://github.com/cszn/KAIR/releases/download/v1.0/ffdnet_color.pth'
WEIGHT_SHA='286f04f7e3a20254c067ba95efb5d3339f4fc8a09a365a93cd469184d4cdb279'
TILE=256;SIDE=TILE//2
torch.set_num_threads(2)

def download(url,path,sha=None):
    data=urllib.request.urlopen(url,timeout=60).read()
    if sha and hashlib.sha256(data).hexdigest()!=sha:raise RuntimeError('Download checksum mismatch')
    path.write_bytes(data)

class Core(nn.Module):
    def __init__(self):
        super().__init__();layers=[nn.Conv2d(13,96,3,padding=1),nn.ReLU()]
        for _ in range(10):layers.extend([nn.Conv2d(96,96,3,padding=1),nn.ReLU()])
        layers.append(nn.Conv2d(96,12,3,padding=1));self.model=nn.Sequential(*layers)
    def forward(self,x):return self.model(x)

def pack(rgb,sigma):
    tensor=torch.from_numpy(rgb.astype(np.float32).transpose(2,0,1)[None]/255)
    down=torch.nn.functional.pixel_unshuffle(tensor,2)
    return torch.cat([down,torch.full((1,1,SIDE,SIDE),float(sigma)/255)],dim=1)

def psnr(a,b):return -10*math.log10(max(float(np.mean((a.astype(float)-b.astype(float))**2)),1e-12)/255**2)

download(WEIGHT_URL,OUT/'ffdnet_color.pth',WEIGHT_SHA)
download('https://raw.githubusercontent.com/cszn/KAIR/master/LICENSE',OUT/'LICENSE-KAIR.txt')
model=Core();model.load_state_dict(torch.load(OUT/'ffdnet_color.pth',map_location='cpu'),strict=True);model.eval()
torch.onnx.export(model,torch.zeros(1,13,SIDE,SIDE),str(OUT/'ffdnet-core.onnx'),
                  input_names=['packed_rgb_sigma'],output_names=['clean_subpixels'],opset_version=12,
                  do_constant_folding=True)

rng=np.random.RandomState(20261024);fixtures=[];dataset=[]
y,x=np.mgrid[:TILE,:TILE]
for index in range(24):
    rgb=np.empty((TILE,TILE,3),np.float32)
    for c in range(3):
        rgb[:,:,c]=40+c*22+90*x/TILE+35*np.sin((x+(c+1)*y)/(5+index%7))+25*((x//(9+c*3)+y//(11+c*2))%2)
    rgb=np.clip(rgb,0,255).astype(np.uint8)
    sigma=[6,12,18,25][index%4]
    noisy=np.clip(rgb.astype(float)+rng.normal(0,sigma,rgb.shape),0,255).astype(np.uint8)
    packed=pack(noisy,sigma)
    calibration=(packed[0].numpy().transpose(1,2,0)*255).round().clip(0,255).astype(np.uint8)
    calibration_path=OUT/f'calibration-{index:02}.npy';np.save(calibration_path,calibration);dataset.append(str(calibration_path.resolve()))
    if index<3:
        with torch.no_grad():prediction=torch.nn.functional.pixel_shuffle(model(packed),2)[0].numpy().transpose(1,2,0)
        prediction=np.clip(prediction*255,0,255).round().astype(np.uint8)
        # Exclude the receptive-field border when checking network quality.
        region=np.s_[28:-28,28:-28,:]
        before,after=psnr(rgb[region],noisy[region]),psnr(rgb[region],prediction[region])
        assert after>before+1,(before,after)
        for label,data in [('clean',rgb),('noisy',noisy),('reference',prediction)]:
            (OUT/f'fixture-{index}-{label}.rgb').write_bytes(data.tobytes())
        fixtures.append({'id':index,'sigma':sigma,'width':TILE,'height':TILE,'noisyPsnr':before,'referencePsnr':after})
(OUT/'dataset.txt').write_text('\n'.join(dataset)+'\n')
report={'model':'FFDNet color','weightsUrl':WEIGHT_URL,'weightsSha256':WEIGHT_SHA,'license':'MIT / Kai Zhang',
        'tile':TILE,'packedInput':[1,13,SIDE,SIDE],'output':[1,12,SIDE,SIDE],'toolkit':'1.5.2','target':'rk3562',
        'fixtures':fixtures,'variants':[],'deviceInferenceVerified':False}
for quantized in [False,True]:
    name='int8' if quantized else 'fp16';rknn=RKNN(verbose=True)
    try:
        result=rknn.config(target_platform='rk3562',mean_values=[[0]*13],std_values=[[255]*13],optimization_level=3)
        assert result==0,result
        assert rknn.load_onnx(model=str(OUT/'ffdnet-core.onnx'))==0
        assert rknn.build(do_quantization=quantized,dataset=str(OUT/'dataset.txt') if quantized else None)==0
        path=OUT/f'ffdnet-color-rk3562-{name}.rknn';assert rknn.export_rknn(str(path))==0
        report['variants'].append({'name':name,'file':path.name,'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
        (OUT/'conversion.json').write_text(json.dumps(report,indent=2))
    finally:rknn.release()
(OUT/'conversion.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
