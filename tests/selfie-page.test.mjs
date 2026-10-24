import test from 'node:test'
import assert from 'node:assert/strict'
import component from '../src/pages/index/index.js'

test('sensor-driven fullscreen restores the previous video mode after an inexact return posture',()=>{
  const v=component.data()
  for(const [name,fn] of Object.entries(component.methods))v[name]=fn.bind(v)
  let time=0,tick,value={available:true,x:2,z:4},enabled=0,disabled=0
  const saved={now:Date.now,setInterval,clearInterval}
  try{
    Date.now=()=>time
    globalThis.setInterval=fn=>{tick=fn;return 123}
    globalThis.clearInterval=()=>{}
    v._visible=true;v.connected=true;v.mode='video'
    v.bridge={native:{getOrientationSensor:()=>value,startOrientationSensor(){enabled++;return true},stopOrientationSensor(){disabled++}}}
    v.startSelfieSensor();time=700;tick()
    value={available:true,x:0,z:5};time=800;tick();time=1500;tick()
    assert.equal(v.selfieActive,true);assert.equal(v.mode,'photo');assert.equal(v._selfieSource,'sensor')
    value={available:true,x:1,z:4};time=1600;tick();time=2300;tick()
    assert.equal(v.selfieActive,false);assert.equal(v.mode,'video');assert.equal(enabled,1)
    v.enterSelfie('manual')
    value={available:true,x:0,z:5};time=2400;tick();time=3100;tick()
    value={available:true,x:1,z:4};time=3200;tick();time=3900;tick()
    assert.equal(v.selfieActive,true);assert.equal(v._selfieSource,'manual')
    v.stopSelfieSensor();assert.ok(disabled>0)
  }finally{
    v.leaveSelfie();Date.now=saved.now
    globalThis.setInterval=saved.setInterval;globalThis.clearInterval=saved.clearInterval
  }
})
