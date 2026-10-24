"""Package an installed verified release, or --local without device-test claims."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,zipfile

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT.parent.parent/'outputs/snapphoto'
version=json.loads((ROOT/'package.json').read_text(encoding='utf-8'))['version']
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--local',action='store_true',help='Package a locally checked build without installation or real-device validation claims')
args=parser.parse_args()
def adb(*args):
    return subprocess.check_output(['adb','-d',*map(str,args)],timeout=20)
def read(path):return json.loads(path.read_text(encoding='utf-8'))
if args.local:
    report=read(ROOT/f'evidence/local-validation-{version}.json')
    assert report['version']==version and report['installed'] is False
    assert report['sourceTests']['failed']==0 and report['sourceTests']['passed']>0
    assert report['nativeAlgorithms']['passed'] and report['nativeBuild'] and report['frontendBuild']
elif version in ['0.9.2','0.9.3']:
    from release_npu import validate
    report=validate(ROOT,version,adb,read)
else:
    registry=json.loads(adb('shell','cat /userdata/miniapp/data/mini_app/pkg/packages.json'))
    package=next(p for p in registry['packages'] if p['appid']=='8001953602710246')
    assert package['version']==version
    private=ROOT/'evidence/private'/version;private.mkdir(parents=True,exist_ok=True)
    installed=private/'installed-app.js.bin'
    adb('pull',package['installPath']+'app.js.bin',installed)
    assert installed.read_bytes()==(ROOT/'dist/app.js.bin').read_bytes()
    prefs=json.loads(adb('shell','cat /userdisk/snapphoto/preferences.json'))
    if version=='0.8.0':
        before=read(ROOT.parent.parent/'backups/snapphoto-0.7.0-before-0.8.0/preferences.json')
        assert all(prefs.get(k)==v for k,v in before.items()),'Existing preferences changed'
    elif version=='0.9.0':
        before=read(ROOT.parent.parent/'backups/snapphoto-0.8.1-before-0.9.0/preferences.json')
        assert prefs==before,'Existing preferences changed'
    elif version=='0.7.0':
        before=read(ROOT.parent.parent/'backups/snapphoto-0.6.0-hd/preferences-before-0.7.0.json')
        assert {k:prefs[k] for k in before}==before and prefs.get('videoProfile')=='hd'
    else:
        before=read(ROOT.parent.parent/('backups/snapphoto-0.5.0-performance/preferences.json' if version=='0.6.0' else 'backups/claude-handoff-2eff90bd/preferences-before-0.5.0.json'))
        assert prefs==before
    native=read(ROOT/f'evidence/profiles-filters-time-{version}.json')
    assert len(native['profiles'])==6 and len(native['filters'])==8 and native['reopen']
    tests=read(ROOT/f'evidence/source-tests-{version}.json');assert tests['failed']==0
    if version in ['0.6.0','0.7.0','0.8.0','0.9.0']:
        installed_libs=adb('shell',"ls '"+package['installPath']+"libs/'libjsapi_snapphoto_*.so").decode().split()
        assert len(installed_libs)==1,installed_libs
        installed_lib=private/'installed-native.so';adb('pull',installed_libs[0],installed_lib)
        native_sha=hashlib.sha256((ROOT/'native/libjsapi_snapphoto.so').read_bytes()).hexdigest()
        # The firmware installer changes SONAME/RUNPATH and relocates ELF metadata.
        # Compare every allocated code/data section, excluding those metadata tables.
        from elftools.elf.elffile import ELFFile
        matching_sections=[]
        with (ROOT/'native/libjsapi_snapphoto.so').open('rb') as a,installed_lib.open('rb') as b:
            source_elf,installed_elf=ELFFile(a),ELFFile(b)
            for section in source_elf.iter_sections():
                if section['sh_flags']&2 and section.name not in ['.dynamic','.dynstr','.dynsym']:
                    actual=installed_elf.get_section_by_name(section.name)
                    assert actual is not None and section.data()==actual.data(),section.name
                    matching_sections.append(section.name)
        assert '.text' in matching_sections and '.rodata' in matching_sections
        if version=='0.9.0':
            optimization=read(ROOT/'evidence/optimization-0.9.0.json')
            resources=read(ROOT/'evidence/resources-0.9.0.json')
            extra=read(ROOT/'evidence/validation-extra-0.9.0.json')
            hd=read(ROOT/'evidence/hd-validation-0.9.0.json')
            assert native['profiles']['four-three']['size']==[3264,2448]
            assert native['preview']['sourceHeight']==2448 and not native['cleanup']['failed']
            assert optimization['runs']['optimized']['reopen'] and extra['realAppLaunched']
            assert extra['nativeAlgorithms']['passed'] and len(hd['takes'])==6 and hd['microphoneRouteRestored']
            report={'version':version,'canonicalSource':str(ROOT),'sourceTests':tests,'photoRegression':native,
                    'optimization':optimization,'hardwareExperiments':resources,'additionalValidation':extra,'hdValidation':hd,
                    'installed':{'version':version,'bytecodeMatchesPackage':True,'nativeCodeDataSectionsMatchPackage':matching_sections,'sourceNativeSha256':native_sha,'installedNativeSha256':hashlib.sha256(installed_lib.read_bytes()).hexdigest()},
                    'preferences':{'existingValuesPreserved':True},
                    'lanDownload':{'tested':False,'reason':'Skipped at user request'},
                    'limits':['Physical rotation, optical focus accuracy and subjective low-light quality not verified this run',
                              'Stock continuous AF observed; no direct lens control or multi-frame fusion',
                              'GPU/RGA experiments are not linked into the release; CPU/NEON remains the production path',
                              'Measured timings depend on scene, filters, load and temperature',
                              'No boot, ADB persistence, governor, driver binding or system camera service changes']}
        elif version=='0.8.0':
            media=read(ROOT/'evidence/gallery-night-0.8.0.json');portrait=read(ROOT/'evidence/portrait-optimized-0.8.0.json');ui=read(ROOT/'evidence/ui-gallery-0.8.0.json')
            assert media['delete']['ownedMediaDeleted'] and media['usbForwardExport']['wavAudio']
            assert portrait['thumbnailJpegCount']==1 and portrait['fps']>20
            assert ui['stockPhotoIndexed'] and ui['videoPlaybackFramesAdvance'] and ui['galleryScrolled']
            report={'version':version,'sourceTests':tests,'photoRegression':native,'mediaValidation':media,'optimizedPortrait':portrait,'realApp':ui,
                    'installed':{'version':version,'bytecodeMatchesPackage':True,'nativeCodeDataSectionsMatchPackage':matching_sections,'sourceNativeSha256':native_sha,'installedNativeSha256':hashlib.sha256(installed_lib.read_bytes()).hexdigest()},
                    'preferences':{'existingValuesPreserved':True,'newDefaults':{'nightMode':'auto','nightPriority':'speed'}},
                    'lanDownload':{'tested':False,'reason':'User requested skipping LAN testing; HTTP tested over USB forward'},
                    'limits':['Software low-light enhancement cannot recover missing sensor detail','MJPEG AVI size and frame rate depend on scene/filter/load','Portrait YUV rotation uses NEON; RGB filters are slower','Stock system album indexes uppercase JPG photos; video playback is in SnapPhoto','No ADB persistence or boot changes; no device reboot']}
        elif version=='0.7.0':
            hd=read(ROOT/'evidence/hd-validation-0.7.0.json');ui_hd=read(ROOT/'evidence/ui-hd-0.7.0.json');ui_fhd=read(ROOT/'evidence/ui-fhd-0.7.0.json')
            assert len(hd['takes'])==6 and hd['fileLimit']['fullDecodePassed'] and hd['microphoneReleased']
            assert ui_hd['decodePassed'] and ui_hd['realUIBackgroundSaved'] and ui_fhd['decodePassed']
            report={
              'version':version,'canonicalSource':str(ROOT),'sourceTests':tests,'photoRegression':native,'hdValidation':hd,
              'realApp720p':ui_hd,'realApp1080p':ui_fhd,'hardwareEncoder':read(ROOT/'evidence/hardware-encoder-0.7.0.json'),'videoFilters':read(ROOT/'evidence/video-filters-0.7.0.json'),
              'installed':{'version':version,'bytecodeMatchesPackage':True,'nativeCodeDataSectionsMatchPackage':matching_sections,'sourceNativeSha256':native_sha,'installedNativeSha256':hashlib.sha256(installed_lib.read_bytes()).hexdigest(),'installerChanges':'ELF SONAME/RUNPATH and symbol/string metadata rewritten by firmware'},
              'preferences':{'existingValuesPreserved':True,'defaultVideoProfile':'hd'},
              'lanDownload':{'tested':False,'reason':'Skipped at user request'},
              'limits':['MJPEG AVI; H264 hardware encoder not available in current kernel configuration',
                        'Actual fps depends on scene, filters, temperature and load; fhd targets 24fps',
                        'Large files; 64MB start-space requirement, 32MB stop threshold and 1.75GiB file limit',
                        'Portrait 1080x1920 slightly upscales the 1836-pixel source crop height',
                        'Kernel, device tree, clocks and persistent USB unlock configuration not changed']
            }
        else:
            recording=read(ROOT/'evidence/recording-stability-0.6.0.json')
            voice=read(ROOT/'evidence/audio-video-0.6.0.json')
            assert recording['busyMicrophoneRejected'] and recording['microphoneRouteRestored'] and recording['cameraReopened']
            assert voice['audioSignal']['samples']>16000 and not voice['decode'] and voice['microphoneAfterStop']=='closed'
            report={
              'version':version,'canonicalSource':str(ROOT),'sourceTests':tests,'nativeValidation':native,
              'performance':read(ROOT/'evidence/performance-0.6.0.json'),'recording':recording,'userVoiceRecording':voice,
              'installed':{'version':version,'bytecodeMatchesPackage':True,'nativeCodeDataSectionsMatchPackage':matching_sections,'sourceNativeSha256':native_sha,'installedNativeSha256':hashlib.sha256(installed_lib.read_bytes()).hexdigest(),'installerChanges':'ELF SONAME/RUNPATH and symbol/string metadata rewritten by firmware'},'ui':read(ROOT/'evidence/ui-0.6.0.json'),
              'preserved':{'preferencesUnchanged':True,'scanGuardSourceUnchanged':True,'roundedIconUnchanged':True,'originalMediaKept':True},
              'lanDownload':{'tested':False,'reason':'Skipped at user request'},
              'limits':['Video remains MJPEG AVI 640x192 landscape or 216x720 portrait',
                        'Stock RKAIQ continuous AF is observed; no tap-to-focus or optical-sharpness improvement claim',
                        'Preview updates are not proof of each frame being presented on the display',
                        'AVI video uses observed wall-clock timing; short final audio block may end earlier',
                        'Audio capture has app-only 4x gain and limiter; no mixer configuration changes']
            }
    else:
        report={
         'version':version,'sourceConversation':'2eff90bd-b747-4f21-8b1e-2c7b311a0040',
         'canonicalSource':str(ROOT),'sourceTests':tests,'nativeValidation':native,
         'installed':{'version':package['version'],'bytecodeMatchesPackage':True,'nativeAsyncCaptureObserved':True},
         'ui':{'launched':True,'ratioPickerSixChoices':True,'completePhotoAspectPreview':True,
               'exportPanelFits280PixelHeight':True,
               'realPhotoWithExif':read(ROOT/f'evidence/ui-photo-{version}.json'),
               'realPhoto16by9':read(ROOT/f'evidence/ui-photo-16-9-{version}.json')},
         'preserved':{'preferencesRestored':True,'scanGuard':True,'roundedIcon':True,'originalMedia':True},
         'lanDownload':{'tested':False,'reason':'Skipped at user request'},
         'limits':['CPU spatial enhancement; no NPU or multi-frame super-resolution',
                   'Time uses the pen clock and frame reception, not hardware exposure time',
                   'Portrait remains 216x720','Sharpness was not compared with stock camera in a controlled scene']
        }
OUT.mkdir(parents=True,exist_ok=True)
for p in [ROOT/f'evidence/release-{version}.json',OUT/f'validation-{version}.json']:
    p.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
amr=ROOT/'dist'/f'SnapPhoto-X7Pro-{version}.amr'
with zipfile.ZipFile(amr) as z:
    assert z.testzip() is None
    manifest=json.loads(z.read('manifest.json'));assert manifest['version']==version
    for name,cert in manifest['cert'].items():
        data=z.read(name);assert len(data)==cert['size'] and hashlib.md5(data).hexdigest()==cert['md5']
shutil.copy2(amr,OUT/amr.name)
files=[ROOT/n for n in ['README.md','LICENSE','.gitignore','package.json','package-lock.json','app_icon.png']]
for folder in ['src','docs','tests','tools','native','assets','.github']:
    files.extend(p for p in (ROOT/folder).rglob('*') if p.is_file() and
                 not any(x in ['cache','__pycache__','node_modules','experiments'] for x in p.relative_to(ROOT).parts) and
                 p.name not in ['snapphoto-probe-new','snapphoto-probe'] and p.suffix!='.so')
for prefix in (['local-validation','release'] if args.local else ['release','source-tests','relaunch'] if version=='0.9.3' else ['release','source-tests','background-zsl','silent-audio-keepalive'] if version=='0.9.2' else ['profiles-filters-time','release','source-tests','optimization','resources','validation-extra','hd-validation'] if version=='0.9.0' else ['profiles-filters-time','release','source-tests','gallery-night','portrait-optimized','ui-gallery'] if version=='0.8.0' else ['profiles-filters-time','release','source-tests','hardware-encoder','hd-validation','ui-hd','ui-fhd','video-filters'] if version=='0.7.0' else ['profiles-filters-time','release','ui','source-tests','performance','audio-video','recording-stability'] if version=='0.6.0' else ['profiles-filters-time','release','ui-photo','ui-photo-16-9','source-tests']):
    files.append(ROOT/f'evidence/{prefix}-{version}.json')
source=OUT/f'SnapPhoto-X7Pro-{version}-source.zip'
with zipfile.ZipFile(source,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(files):z.write(p,f'SnapPhoto-X7Pro-{version}/'+p.relative_to(ROOT).as_posix())
with zipfile.ZipFile(source) as z:
    assert z.testzip() is None
    assert not any('/private/' in name or '/cache/' in name for name in z.namelist())
artifacts=[OUT/amr.name,source,OUT/f'validation-{version}.json']
(OUT/f'SHA256SUMS-{version}.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in artifacts),encoding='utf-8')
shutil.copy2(ROOT/'README.md',OUT/'README.md')
print(json.dumps({'installedVersion':None if args.local else version,'installedBytecodeMatches':None if args.local else True,'localOnly':args.local,
                  'sourceFiles':len(files),'artifacts':[{'name':p.name,'bytes':p.stat().st_size} for p in artifacts]}))
