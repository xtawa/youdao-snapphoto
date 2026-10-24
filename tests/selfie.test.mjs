import test from 'node:test'
import assert from 'node:assert/strict'
import {DoubleTap,FlipTracker} from '../src/utils/selfie.js'
const event=(x,y)=>({changedTouches:[{pageX:x,pageY:y}]})
function tap(d,x,y,start,end){d.start(event(x,y),start);return d.end(event(x,y),end)}
test('double tap accepts different positions and fires once for a rapid burst of taps',()=>{
  const d=new DoubleTap()
  assert.equal(tap(d,100,100,1000,1050),false)
  assert.equal(tap(d,876,140,1200,1250),true)
  assert.equal(tap(d,876,140,1350,1400),false)
  assert.equal(tap(d,876,140,1450,1500),false)
})
test('swipes, long contacts and widely separated taps do not shoot',()=>{
  const d=new DoubleTap();d.start(event(600,130),1000);assert.equal(d.end(event(300,130),1200),false)
  assert.equal(tap(d,100,100,1400,1900),false)
  assert.equal(tap(d,100,100,2000,2050),false)
  assert.equal(tap(d,100,100,2600,2650),false)
})
test('cancel/reset discards the previous tap',()=>{
  const d=new DoubleTap();tap(d,100,100,1000,1050);d.reset()
  assert.equal(tap(d,100,100,1100,1150),false)
})
test('flip requires a stable opposite direction; level yaw and quarter turns are ignored',()=>{
  const f=new FlipTracker(),s=(x,z,t)=>f.sample({available:true,x,z},t)
  s(2,4,0);s(2,4,700)
  assert.equal(s(0,4,800),null);assert.equal(s(0,4,1600),null)
  assert.equal(s(3,4,1700),null);assert.equal(s(3,4,2000),null);assert.equal(s(3,4,2400),true)
  assert.equal(s(3,4,2800),null)
  assert.equal(s(2,4,3000),null);assert.equal(s(2,4,3700),false)
})
test('face-up to face-down flip and missing sensor fallback',()=>{
  const f=new FlipTracker();assert.equal(f.sample({available:false},0),null)
  f.sample({available:true,x:2,z:4},100);f.sample({available:true,x:2,z:4},800)
  f.sample({available:true,x:2,z:5},900);assert.equal(f.sample({available:true,x:2,z:5},1600),true)
})
test('returning from selfie accepts a changed secondary axis without waiting for exact baseline',()=>{
  const f=new FlipTracker(),s=(x,z,t)=>f.sample({available:true,x,z},t)
  s(2,4,0);s(2,4,700)
  s(0,5,800);assert.equal(s(0,5,1500),true)
  s(1,4,1600);assert.equal(s(1,4,1900),null);assert.equal(s(1,4,2300),false)
  assert.equal(s(1,4,3000),null)
})
