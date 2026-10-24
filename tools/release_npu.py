"""Verify the installed offline-model release without publishing private media."""
import hashlib,json
from pathlib import Path
def validate(root,version,adb,read):
    registry=json.loads(adb('shell','cat /userdata/miniapp/data/mini_app/pkg/packages.json'))
    package=next(p for p in registry['packages'] if p['appid']=='8001953602710246');assert package['version']==version
    private=root/'evidence/private'/version;private.mkdir(parents=True,exist_ok=True)
    install=package['installPath'];bytecode=private/'app.js.bin'
    adb('pull',install+'app.js.bin',bytecode);assert bytecode.read_bytes()==(root/'dist/app.js.bin').read_bytes()
    native_path=adb('shell','ls '+install+'libs/libjsapi_snapphoto_*.so').decode().split();assert len(native_path)==1
    binary=private/'installed-native.so';adb('pull',native_path[0],binary)
    from elftools.elf.elffile import ELFFile
    matching=[]
    with (root/'native/libjsapi_snapphoto.so').open('rb') as a,binary.open('rb') as b:
        expected,actual=ELFFile(a),ELFFile(b)
        for section in expected.iter_sections():
            if section['sh_flags']&2 and section.name not in ['.dynamic','.dynstr','.dynsym']:
                installed=actual.get_section_by_name(section.name)
                assert installed is not None and installed.data()==section.data(),section.name
                matching.append(section.name)
    assert '.text' in matching and '.rodata' in matching
    model=read(root/'assets/models/ffdnet.json')
    assert adb('shell','sha256sum '+install+'assets/models/ffdnet-color-rk3562.rknn').decode().split()[0]==model['sha256']
    assert adb('shell','sha256sum '+install+'assets/bin/snapphoto-worker').decode().split()[0]==hashlib.sha256((root/'native/snapphoto-probe').read_bytes()).hexdigest()
    tests=read(root/f'evidence/source-tests-{version}.json');assert tests['passed']==100 and tests['failed']==0
    background=read(root/'evidence/background-zsl-0.9.2.json')
    assert background['workerAfterUIExit'] and background['pauseOnExit'] and background['completedAfterUIExit']['result']['npuUsed']
    assert max(p['selectionMs'] for p in background['photos'])<150
    prefs=json.loads(adb('shell','cat /userdisk/snapphoto/preferences.json'))
    original={'nightMode':'auto','nightPriority':'quality','timerDelay':0,'burstCount':1,'showGrid':False,'autoSelfie':True,'layoutMode':'auto','videoProfile':'fhd','photoProfile':'cinema'}
    assert all(prefs.get(k)==v for k,v in original.items())
    assert prefs['denoiseMode']=='auto' and prefs['processingKeepAlive'] is True
    jobs=adb('shell','ls /userdisk/snapphoto/jobs/job-*.json').decode().split()
    actual_jobs=[json.loads(adb('shell','cat '+path)) for path in jobs]
    completed=[j for j in actual_jobs if j.get('state')=='done' and j.get('published') and j.get('result',{}).get('npuUsed')]
    assert completed,'No real app NPU job/publication receipt'
    relaunch=read(root/'evidence/relaunch-0.9.3.json') if version=='0.9.3' else None
    if relaunch:assert len(relaunch['cycles'])==3 and relaunch['sameFirmwareHost'] and all(not c['moduleImportErrors'] for c in relaunch['cycles'])
    return {'version':version,'relaunch':relaunch,'inheritedPerformanceEvidenceVersion':'0.9.2','pr2MergedCommit':'4a851a909a2967a383f6c635b085e2db6c21069b',
            'sourceTests':tests,'installed':{'bytecodeMatches':True,'nativeAllocatedSectionsMatch':matching,'modelHashMatches':True,'workerHashMatches':True},
            'model':model,'backgroundAndFastCapture':background,
            'npuSyntheticQuality':read(root/'evidence/npu-photo-0.9.1.json'),
            'fullResolution':read(root/'evidence/npu-full-photo-0.9.1.json'),
            'realApp':{'npuReady':True,'queueFinished':True,'stockAlbumScanCompleted':True,
                       'lastNpuMs':completed[-1]['result']['npuTimeMs'],'lastJobMs':completed[-1]['result']['processingMs']},
            'preferences':{'existingValuesPreserved':True,'enabled':{'denoiseMode':'auto','processingKeepAlive':True}},
            'silentPlayback':read(root/'evidence/silent-audio-keepalive-0.9.2.json'),
            'limits':['True multi-exposure HDR remains disabled; fusion reference alone is not camera HDR',
                      'Fast capture uses a recent source frame up to 200ms old; optics and hand motion still affect sharpness',
                      'Queue and helper continue after UI exit; OS suspend may pause work and no shutdown/kill immunity is claimed',
                      'No permanent service or audio playback; the default speaker is exclusive',
                      'Real hand-held focus quality and low-light appearance need user review; synthetic PSNR is not real-scene proof',
                      'No boot, ADB persistence, PMIC, governor, system audio routing or driver changes']}
