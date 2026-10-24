import test from 'node:test'
import assert from 'node:assert/strict'
import {CameraBridge} from '../src/platform/camera-bridge.js'

test('duplicate preview paths do not repaint and native timing survives',async()=>{
  let image='file://frame-1.jpg'
  const b=new CameraBridge({getCapabilities:()=>({camera:true,preview:true}),openPreview(){},getPreviewFrame:()=>({image,previewFps:27,sequence:1})})
  await b.connect();await b.openPreview()
  assert.equal(await b.getPreviewFrame(),image)
  assert.equal(await b.getPreviewFrame(),null)
  assert.equal(b.frameInfo.previewFps,27)
  image='file://frame-2.jpg';assert.equal(await b.getPreviewFrame(),image)
  await b.openPreview();assert.equal(await b.getPreviewFrame(),image)
})

test('failed video finalization clears recording state and allows another take',async()=>{
  let first=true
  const b=new CameraBridge({getCapabilities:()=>({camera:true,video:true,audio:true}),startRecording:()=>({audio:true}),stopRecording(){if(first){first=false;throw new Error('incomplete')}return {path:'/userdisk/Pictures/video.avi',written:true}}})
  await b.connect();await b.startRecording()
  await assert.rejects(b.stopRecording(),/incomplete/)
  assert.equal(b.recording,false)
  await b.startRecording();await b.stopRecording();assert.equal(b.recording,false)
})

test('a claimed audio recorder cannot silently start a mute video',async()=>{
  let stopped=0
  const b=new CameraBridge({getCapabilities:()=>({camera:true,video:true,audio:true}),startRecording:()=>({audio:false}),stopRecording(){stopped++}})
  await b.connect();await assert.rejects(b.startRecording(),/麦克风/)
  assert.equal(stopped,1);assert.equal(b.recording,false)
})

test('photo waits for continuous AF settling and cancels after camera closes',async()=>{
  let resolve,photos=0
  const b=new CameraBridge({getCapabilities:()=>({camera:true,photo:true,preview:true,continuousFocus:true}),openPreview(){},closePreview(){},waitForAutoFocus:()=>new Promise(r=>{resolve=r}),capturePhoto(){photos++;return {path:'/userdisk/Pictures/test.jpg',written:true}}})
  await b.connect();await b.openPreview()
  const pending=b.capturePhoto();await b.close();resolve({settled:true})
  await assert.rejects(pending,/相机已关闭/);assert.equal(photos,0)
})

test('burst shots can skip repeating the AF delay',async()=>{
  let waits=0
  const b=new CameraBridge({getCapabilities:()=>({camera:true,photo:true,preview:true,continuousFocus:true}),openPreview(){},waitForAutoFocus(){waits++;return {settled:true}},capturePhoto:()=>({path:'/userdisk/Pictures/test.jpg',written:true})})
  await b.connect();await b.openPreview()
  await b.capturePhoto();await b.capturePhoto({settleFocus:false})
  assert.equal(waits,1);assert.equal(b.caps.focus,false)
})

test('an unsettled focus timeout never proceeds to capture',async()=>{
  let shots=0
  const b=new CameraBridge({getCapabilities:()=>({camera:true,photo:true,preview:true,continuousFocus:true}),openPreview(){},waitForAutoFocus:()=>({settled:false}),capturePhoto(){shots++;return {path:'/userdisk/Pictures/test.jpg'}}})
  await b.connect();await b.openPreview()
  await assert.rejects(b.capturePhoto(),/焦点尚未稳定/);assert.equal(shots,0)
})
