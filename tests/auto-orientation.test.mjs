import test from 'node:test'
import assert from 'node:assert/strict'
import {OrientationTracker,sensorRotation,displayRotation,captureRotation} from '../src/utils/auto-orientation.js'
import page from '../src/pages/index/index.js'

test('stock sensor classifications map four directions and ignore face up/down',()=>{
  assert.deepEqual([0,1,2,3,4,5].map(x=>sensorRotation({available:true,x})),[0,180,90,270,null,null])
  assert.equal(sensorRotation({available:false,x:2}),null)
  const t=new OrientationTracker()
  assert.equal(t.sample({available:true,x:2},0),null)
  assert.equal(t.sample({available:true,x:0},400),null)
  assert.equal(t.sample({available:true,x:2},600),null)
  assert.equal(t.sample({available:true,x:2},1100),90)
  assert.equal(t.sample({available:true,x:4},1800),null)
  assert.equal(t.rotation,90)
})
test('auto orientation works when automatic selfie is disabled and freezes while recording',()=>{
  const v=page.data();for(const [k,f] of Object.entries(page.methods))v[k]=f.bind(v)
  Object.defineProperty(v,'orientation',{get:()=>page.computed.orientation.call(v)})
  v.autoSelfie=false;v._visible=true;v.connected=true;let value={available:true,x:2,z:4},tick,now=0,rotations=[]
  const saved={setInterval,clearInterval,now:Date.now}
  try{
    globalThis.setInterval=f=>{tick=f;return 1};globalThis.clearInterval=()=>{};Date.now=()=>now
    v.bridge={native:{getOrientationSensor:()=>value,startOrientationSensor:()=>true,setOrientation:(...a)=>rotations.push(a)}}
    v.startSelfieSensor();now=600;tick()
    assert.equal(v.detectedOrientation,'portrait');assert.equal(v.rotation,90)
    v.recording=true;value={available:true,x:1,z:4};now=1200;tick();now=2000;tick()
    assert.equal(v.rotation,90)
    v.recording=false;now=2100;tick();now=2700;tick()
    assert.deepEqual(rotations,[['portrait',270],['landscape',180]])
    assert.equal(v.selfieActive,false)
  }finally{globalThis.setInterval=saved.setInterval;globalThis.clearInterval=saved.clearInterval;Date.now=saved.now}
})
test('portrait pixels undo the UI transform, including forced portrait and both sensor directions',()=>{
  for(const mode of ['auto','portrait','landscape'])for(const rotation of [0,90,180,270]){
    const orientation=mode==='auto'?(rotation%180?'portrait':'landscape'):mode
    const ui=displayRotation(mode,orientation,rotation),pixels=captureRotation(mode,orientation,rotation)
    assert.equal((ui+pixels)%360,0,mode+' '+rotation)
    assert.equal(pixels%180,orientation==='portrait'?90:0)
  }
})
test('connect, layout changes and resume apply the same inverse capture rotation',()=>{
  const v=page.data();for(const [k,f] of Object.entries(page.methods))v[k]=f.bind(v)
  Object.defineProperty(v,'orientation',{get:()=>page.computed.orientation.call(v)})
  const calls=[];v.bridge={native:{setOrientation:(...args)=>calls.push(args)}}
  v.persist=()=>{};v.detectedOrientation='portrait';v.rotation=270
  v.applyOrientation();v.cycleLayout();v.cycleLayout();v.cycleLayout()
  assert.deepEqual(calls,[['portrait',90],['portrait',270],['landscape',0],['portrait',90]])
})
test('returning to auto uses the stable sensor posture sampled while layout was forced',()=>{
  const v=page.data();for(const [k,f] of Object.entries(page.methods))v[k]=f.bind(v)
  Object.defineProperty(v,'orientation',{get:()=>page.computed.orientation.call(v)})
  v.layoutMode='landscape';v._orientationTracker={rotation:270};v.persist=()=>{}
  const calls=[];v.bridge={native:{setOrientation:(...args)=>calls.push(args)}}
  v.cycleLayout();assert.equal(v.orientation,'portrait');assert.deepEqual(calls,[['portrait',90]])
})
