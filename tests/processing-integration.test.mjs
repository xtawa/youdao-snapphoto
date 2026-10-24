import test from 'node:test'
import assert from 'node:assert/strict'
import {CameraBridge} from '../src/platform/camera-bridge.js'
import component from '../src/pages/index/index.js'
import {renderDeviceUI} from '../src/pages/index/device-ui.js'
import {normalizePreferences} from '../src/utils/preferences.js'

test('HDR preserves the X7 Pro autofocus wait and fast subsequent burst frames',async()=>{
  const calls=[]
  const bridge=new CameraBridge({
    getCapabilities:()=>({camera:true,photo:true,preview:true,continuousFocus:true,hdr:true,hdrStrategies:['bracket']}),
    openPreview(){},waitForAutoFocus(){calls.push('focus');return {settled:true}},
    captureHDR(options){calls.push(options);return {path:'/userdisk/Pictures/hdr.JPG',hdrApplied:true,hdrBackend:'bracket'}}
  })
  await bridge.connect();await bridge.openPreview()
  await bridge.capturePhoto({hdrMode:'on',settleFocus:true})
  await bridge.capturePhoto({hdrMode:'on',settleFocus:false})
  assert.equal(calls.filter(x=>x==='focus').length,1)
  assert.deepEqual(calls[1].exposuresEV,[-2,0,2]);assert.equal(calls[2].settleFocus,false)
})

test('hiding during autofocus prevents starting an HDR capture',async()=>{
  let finish,called=false
  const bridge=new CameraBridge({
    getCapabilities:()=>({camera:true,photo:true,continuousFocus:true,hdr:true,hdrStrategies:['isp']}),
    waitForAutoFocus:()=>new Promise(resolve=>{finish=resolve}),
    captureHDR(){called=true}
  })
  await bridge.connect();bridge.previewOpen=true
  const capture=bridge.capturePhoto({hdrMode:'on'})
  bridge.previewOpen=false;finish({settled:true})
  await assert.rejects(capture,/相机已关闭/);assert.equal(called,false)
})

test('an invalid enhanced result retains the original photo and reports a warning',async()=>{
  const bridge=new CameraBridge({
    getCapabilities:()=>({camera:true,photo:true,enhancement:true}),
    capturePhoto:()=>({path:'/userdisk/Pictures/original.JPG',indexed:true}),
    enhancePhoto:()=>({path:'https://example.com/photo.jpg',written:true,denoiseApplied:true})
  })
  await bridge.connect()
  const result=await bridge.capturePhoto({denoiseMode:'quality'})
  assert.equal(result.path,'/userdisk/Pictures/original.JPG');assert.equal(result.indexed,true)
  assert.match(result.galleryWarning,/保留原片/)
})

test('a different enhanced file does not inherit the original stock album index receipt',async()=>{
  const bridge=new CameraBridge({
    getCapabilities:()=>({camera:true,photo:true,enhancement:true}),
    capturePhoto:()=>({path:'/userdisk/Pictures/original.JPG',indexed:true}),
    enhancePhoto:()=>({path:'/userdisk/Pictures/enhanced.JPG',written:true,denoiseApplied:true})
  })
  await bridge.connect()
  const result=await bridge.capturePhoto({denoiseMode:'quality'})
  assert.equal(result.indexed,false);assert.equal(result.denoiseApplied,true)
})

test('NPU enhancement forwards capture time and progress and preserves its real receipt',async()=>{
  let options,progress=0
  const bridge=new CameraBridge({
    getCapabilities:()=>({camera:true,photo:true,enhancement:true,npu:true,npuReady:true}),
    capturePhoto:()=>({path:'/userdisk/Pictures/original.JPG',capturedAtEpochMs:1780000000123,size:900}),
    enhancePhoto:value=>{options=value;value.onProcessing(42);return {path:'/userdisk/Pictures/ai.JPG',written:true,npuUsed:true,denoiseApplied:true,size:400,tiles:6,npuTimeMs:220}}
  })
  await bridge.connect();const result=await bridge.capturePhoto({denoiseMode:'quality',onProcessing:value=>{progress=value}})
  assert.equal(options.capturedAtEpochMs,1780000000123);assert.equal(options.preserveOriginal,true)
  assert.equal(progress,42);assert.equal(result.size,400);assert.equal(result.npuUsed,true);assert.equal(result.tiles,6);assert.equal(result.npuTimeMs,220)
})

test('ZSL capture bypasses autofocus and returns without waiting for album indexing or NPU',async()=>{
  let focus=0,enhanced=0,finishPublication,queued
  const bridge=new CameraBridge({
    getCapabilities:()=>({camera:true,photo:true,continuousFocus:true,zeroShutterLag:true,enhancement:true,npu:true,npuReady:true,backgroundEnhancement:true}),
    waitForAutoFocus(){focus++;return {settled:false}},
    capturePhoto:()=>({path:'/userdisk/Pictures/fast.JPG',written:true,capturedAtEpochMs:1780000000123}),
    publishToGallery:()=>new Promise(resolve=>{finishPublication=resolve}),
    enhancePhoto(){enhanced++;return new Promise(()=>{})},
    queueEnhancement(options){queued=options;return {queued:true,jobId:'job-1.json'}}
  })
  await bridge.connect()
  const result=await bridge.capturePhoto({denoiseMode:'quality',processingKeepAlive:false})
  assert.equal(focus,0);assert.equal(enhanced,0);assert.equal(result.enhancementQueued,true)
  assert.equal(result.publicationPending,true);assert.equal(result.indexed,false)
  assert.equal(queued.keepAlive,false);assert.equal(queued.capturedAtEpochMs,1780000000123)
  finishPublication({path:result.path,written:true,indexed:true});await Promise.resolve()
})

test('legacy settings keep photo, video, night and selfie preferences with processing disabled',()=>{
  const p=normalizePreferences({photoProfile:'four-three',videoProfile:'fhd',nightPriority:'quality',autoSelfie:false})
  assert.equal(p.photoProfile,'four-three');assert.equal(p.videoProfile,'fhd')
  assert.equal(p.nightPriority,'quality');assert.equal(p.autoSelfie,false)
  assert.equal(p.hdrMode,'off');assert.equal(p.denoiseMode,'off')
})

function page(orientation){
  const vm=component.data()
  for(const [key,fn] of Object.entries(component.methods))vm[key]=fn.bind(vm)
  for(const [key,fn] of Object.entries(component.computed))Object.defineProperty(vm,key,{get:()=>fn.call(vm)})
  vm.layoutMode=orientation;vm.panel='settings';vm.settingsPage=5
  vm.message=value=>{vm.lastMessage=value};vm.persist=()=>{vm.saved=true}
  return vm
}

test('processing settings fit both small-screen orientations and guard unsupported hardware',()=>{
  const h=(tag,data,children)=>({tag,data,children})
  for(const orientation of ['landscape','portrait']){
    const vm=page(orientation),root=renderDeviceUI.call(vm,h)
    const text=[]
    function walk(node){if(typeof node==='string')text.push(node);else if(node&&node.children){if(Array.isArray(node.children))node.children.forEach(walk);else walk(node.children)}}
    walk(root)
    assert.ok(text.includes('HDR 拍照'));assert.ok(text.includes('拍后降噪'));assert.ok(text.includes('NPU 增强'))
    assert.ok(text.includes('6 / 7'));assert.ok(text.includes('未开放'))
    const width=orientation==='portrait'?280:936,height=orientation==='portrait'?936:280
    for(const node of root.children){const s=node.data.style;assert.ok(parseFloat(s.left)+parseFloat(s.width)<=width);assert.ok(parseFloat(s.top)+parseFloat(s.height)<=height)}
    vm.cycleHDR();assert.equal(vm.hdrMode,'off');assert.match(vm.lastMessage,/尚未接入/)
    vm.cycleDenoise();assert.equal(vm.denoiseMode,'off');assert.match(vm.lastMessage,/暗光增强仍可/)
    assert.equal(vm.saved,undefined)
    vm.caps={hdr:true,enhancement:true,npuReady:true}
    vm.cycleHDR();vm.cycleDenoise()
    assert.equal(vm.hdrMode,'auto');assert.equal(vm.denoiseMode,'auto');assert.equal(vm.saved,true)
    vm.recording=true;vm.cycleHDR();assert.equal(vm.hdrMode,'auto')
  }
})
