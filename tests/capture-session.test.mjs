import test from 'node:test'
import assert from 'node:assert/strict'
import {CaptureSession,validCaptureOptions} from '../src/utils/capture-session.js'
test('validates timer and burst values',()=>{
  assert.deepEqual(validCaptureOptions({delay:3,count:5}),{delay:3,count:5})
  assert.throws(()=>validCaptureOptions({delay:-1,count:1}),RangeError)
  assert.throws(()=>validCaptureOptions({delay:3,count:200}),RangeError)
})
test('countdown then burst sequences exact photos',async()=>{
  const ticks=[]
  const clock={setTimeout(cb){queueMicrotask(cb);return 1},clearTimeout(){}}
  const session=new CaptureSession(clock)
  const result=await session.run({delay:3,count:3,onTick:t=>ticks.push(t),capture:async i=>({path:i+'.jpg'})})
  assert.equal(result.completed,3)
  assert.deepEqual(ticks,[3,2,1,0,0])
})
test('cancel during countdown never calls capture',async()=>{
  let calls=0
  const clock={setTimeout(){return 12},clearTimeout(){}}
  const session=new CaptureSession(clock)
  const task=session.run({delay:5,count:3,capture:async()=>{calls++}})
  session.cancel()
  const result=await task
  assert.equal(calls,0)
  assert.equal(result.completed,0)
  assert.equal(result.cancelled,true)
})
test('rejects overlapping tasks',async()=>{
  let done
  const s=new CaptureSession()
  const p=s.run({delay:0,count:1,capture:()=>new Promise(resolve=>{done=resolve})})
  await assert.rejects(s.run({delay:0,count:1,capture:()=>{}}),/正在执行/)
  done({path:'ok'})
  assert.equal((await p).completed,1)
})
