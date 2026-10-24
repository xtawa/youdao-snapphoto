import test from 'node:test'
import assert from 'node:assert/strict'
import {createOrientationSensor} from '../src/platform/orientation-sensor.js'

test('orientation sensor pairs stock subscription and wakeup lifecycle and ignores stale events',()=>{
  let listener;const calls=[]
  class Gsensor{
    orientResChange={on(fn){listener=fn;calls.push('on')},off(fn){assert.equal(fn,listener);calls.push('off')}}
    wakeupEnable(){calls.push('enable')}
    wakeupDisable(){calls.push('disable')}
  }
  let current={available:true,x:2,z:4}
  const sensor=createOrientationSensor({Gsensor},()=>current)
  assert.equal(sensor.read().available,false)
  assert.equal(sensor.start(),true);sensor.start()
  assert.equal(sensor.read().x,2)
  listener('{"x":3,"y":2,"z":5}');assert.equal(sensor.read().x,2)
  current={available:false};assert.equal(sensor.read().x,3)
  listener('invalid');listener({});assert.equal(sensor.read().x,3)
  sensor.stop();sensor.stop();listener({x:2,y:2,z:4})
  assert.equal(sensor.read().available,false)
  assert.deepEqual(calls,['on','enable','disable','off'])
  current={available:true,x:2,z:4};sensor.start();assert.equal(sensor.read().x,2);sensor.stop()
})
test('orientation sensor removes its subscription when startup fails',()=>{
  const calls=[]
  class Gsensor{
    orientResChange={on(){calls.push('on')},off(){calls.push('off')}}
    wakeupEnable(){throw new Error('unavailable')}
  }
  const sensor=createOrientationSensor({default:{Gsensor}},()=>({available:true}))
  assert.throws(()=>sensor.start(),/unavailable/)
  assert.equal(sensor.read().available,false)
  assert.deepEqual(calls,['on','off'])
  assert.equal(createOrientationSensor({},()=>{}).start(),false)
})
