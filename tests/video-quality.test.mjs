import test from 'node:test'
import assert from 'node:assert/strict'
import {CameraBridge} from '../src/platform/camera-bridge.js'
import {normalizePreferences,loadPreferences,savePreferences} from '../src/utils/preferences.js'
import {VIDEO_PROFILES,PHOTO_PROFILES} from '../src/utils/format.js'
import {previewRect} from '../src/utils/photo-layout.js'
import {renderDeviceUI} from '../src/pages/index/device-ui.js'
import component from '../src/pages/index/index.js'

test('old preferences migrate to 720p while existing photo and orientation survive',async()=>{
  const old={timerDelay:5,burstCount:3,photoProfile:'square',layoutMode:'portrait'}
  const p=normalizePreferences(old)
  assert.equal(p.videoProfile,'hd');assert.equal(p.photoProfile,'square');assert.equal(p.layoutMode,'portrait')
  assert.equal(normalizePreferences({videoProfile:'unknown'}).videoProfile,'hd')
  let saved
  const root={SnapPhotoNative:{readPreferences:()=>saved||old,writePreferences:p=>{saved=p}}}
  await savePreferences({...p,videoProfile:'fhd'},root)
  assert.equal((await loadPreferences(root)).videoProfile,'fhd')
})

test('video quality accepts only advertised profiles and stays locked while recording',async()=>{
  const calls=[]
  const b=new CameraBridge({getCapabilities:()=>({camera:true,video:true,videoProfiles:VIDEO_PROFILES}),setVideoProfile:id=>calls.push(id)})
  await b.connect();await b.setVideoProfile('fhd')
  await assert.rejects(b.setVideoProfile('4k'),/不支持/)
  b.recording=true;await assert.rejects(b.setVideoProfile('compact'),/结束录像/)
  assert.deepEqual(calls,['fhd'])
})

test('video previews use selected native aspect ratio independently of photo preferences',()=>{
  for(const orientation of ['landscape','portrait'])for(const p of VIDEO_PROFILES){
    const r=previewRect(orientation,'video',PHOTO_PROFILES,'square',VIDEO_PROFILES,p.id)
    const ratio=orientation==='portrait'?p.portraitWidth/p.portraitHeight:p.width/p.height
    assert.ok(Math.abs(r.width/r.height-ratio)<1e-8)
    const w=orientation==='portrait'?280:936,h=orientation==='portrait'?936:280
    assert.ok(r.x>=0&&r.y>=0&&r.x+r.width<=w+.001&&r.y+r.height<=h+.001)
  }
})

test('video quality choices fit both orientations on the pen',()=>{
  const h=(tag,data,children)=>({tag,...data,children})
  for(const orientation of ['landscape','portrait']){
    const tree=renderDeviceUI.call({orientation,panel:'videoQuality',videoProfileList:VIDEO_PROFILES,videoProfile:'hd',applyVideoProfile(){},screenStyle:{}},h)
    const w=orientation==='portrait'?280:936,ht=orientation==='portrait'?936:280
    for(const n of tree.children){
      const s=n.style;if(!s||s.position!=='absolute')continue
      assert.ok(parseFloat(s.left)>=0&&parseFloat(s.top)>=0)
      assert.ok(parseFloat(s.left)+parseFloat(s.width)<=w+.001)
      assert.ok(parseFloat(s.top)+parseFloat(s.height)<=ht+.001)
    }
  }
})

test('recording limit is still handled while a settings panel is open',async()=>{
  const vm=component.data();for(const [name,fn] of Object.entries(component.methods))vm[name]=fn.bind(vm)
  let stopped=0,resolve
  const handled=new Promise(r=>{resolve=r})
  vm._visible=true;vm.connected=true;vm.recording=true;vm.panel='settings';vm.sessionId=1
  vm.bridge={frameInfo:{recordError:true,recordLimit:true},getPreviewFrame:async()=> 'new-frame.jpg'}
  vm.message=()=>{};vm.shutter=()=>{stopped++;clearInterval(vm.frameTimer);resolve()}
  vm.poll()
  try{await Promise.race([handled,new Promise((_,reject)=>setTimeout(()=>reject(new Error('limit was ignored')),500))])}
  finally{clearInterval(vm.frameTimer)}
  assert.equal(stopped,1);assert.equal(vm.preview,'')
})
