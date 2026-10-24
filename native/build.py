"""Build C camera code separately from the C++ RKNN adapter."""
from pathlib import Path
import os,subprocess,argparse
root=Path(__file__).resolve().parent
ap=argparse.ArgumentParser();ap.add_argument('--zig',required=True);args=ap.parse_args()
env=dict(os.environ);env['ZIG_GLOBAL_CACHE_DIR']=str(root/'cache/global');env['ZIG_LOCAL_CACHE_DIR']=str(root/'cache/local')
target=['-target','aarch64-linux-gnu.2.29','-O2','-fPIC','-fvisibility=hidden']
objects=root/'cache/objects';objects.mkdir(parents=True,exist_ok=True)
def run(command):subprocess.run([args.zig]+command,env=env,check=True)
run(['cc']+target+['-I',str(root/'vendor'),'-c',str(root/'vendor/cJSON.c'),'-o',str(objects/'cjson.o')])
run(['c++']+target+['-std=c++17','-Wall','-Wextra','-Werror','-DSNAPPHOTO_HAVE_RKNN=1','-I',str(root/'vendor/rknn'),'-c',str(root/'rknn/rknn_enhancer.cpp'),'-o',str(objects/'npu.o')])
for cli in [False,True]:
    name='cli' if cli else 'module'
    run(['cc']+target+(['-DSNAP_CLI'] if cli else [])+['-I',str(root/'vendor'),'-c',str(root/'snapphoto.c'),'-o',str(objects/(name+'.o'))])
    run(['c++']+target+(['-shared'] if not cli else [])+[str(objects/(name+'.o')),str(objects/'cjson.o'),str(objects/'npu.o'),'-lpthread','-ldl','-lm','-o',str(root/('snapphoto-probe' if cli else 'libjsapi_snapphoto.so'))])
run(['cc']+target+[str(root/'processing-test.c'),'-lpthread','-lm','-o',str(root/'processing-test')])
