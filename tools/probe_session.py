"""One explicitly started probe, shut down through its own protocol."""
import json,queue,subprocess,threading,time
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
def adb(*args):
    return subprocess.check_output(['adb','-d',*map(str,args)],timeout=20).decode('utf-8',errors='replace')
class Probe:
    def __init__(self,binary=None,model=None,worker=None):
        self.binary=binary or ROOT/'native/snapphoto-probe'
        self.model=model
        self.worker=worker
    def __enter__(self):
        adb('push',self.binary,'/tmp/snapphoto-validation')
        adb('shell','chmod 700 /tmp/snapphoto-validation')
        values=(['SNAPPHOTO_MODEL='+self.model] if self.model else [])+(['SNAPPHOTO_WORKER='+self.worker] if self.worker else [])
        command=['adb','-d','shell']+(['env']+values if values else [])+['/tmp/snapphoto-validation']
        self.p=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
        self.lines=queue.Queue()
        def read():
            for line in self.p.stdout:self.lines.put(line)
            self.lines.put(None)
        threading.Thread(target=read,daemon=True).start();return self
    def call(self,op,allow_error=False,**kw):
        self.p.stdin.write(json.dumps(dict(op=op,**kw))+'\n');self.p.stdin.flush();until=time.monotonic()+20
        while time.monotonic()<until:
            line=self.lines.get(timeout=max(.01,until-time.monotonic()))
            if line is None:raise RuntimeError("Native probe exited: "+str(self.p.poll()))
            try:r=json.loads(line)
            except ValueError:continue
            if isinstance(r,dict) and 'op' in r:continue
            if not allow_error and isinstance(r,dict) and r.get('error'):raise RuntimeError(op+': '+r['error'])
            return r
        raise TimeoutError(op)
    def photo(self):
        self.call('photoStart')
        until=time.monotonic()+45
        while time.monotonic()<until:
            result=self.call('photoResult')
            if not result.get('pending'):return result
            time.sleep(.1)
        raise TimeoutError('photo processing')
    def wait_frame(self):
        for _ in range(40):
            frame=self.call('frame')
            if frame.get('image'):return frame
            time.sleep(.15)
        raise RuntimeError('No frame; inspect ownership. Do not automatically reset drivers.')
    def __exit__(self,*exc):
        try:
            if self.p.poll() is not None:return False
            self.call('stopShare');self.call('close')
            self.p.stdin.write('{"op":"quit"}\n');self.p.stdin.flush();self.p.stdin.close();self.p.wait(timeout=12)
        finally:
            if self.p.poll() is None:
                self.p.terminate();raise RuntimeError('Probe did not exit cleanly; inspect before retrying')
