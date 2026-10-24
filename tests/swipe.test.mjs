import test from 'node:test'
import assert from 'node:assert/strict'
import {touchPoint,swipeStep} from '../src/utils/swipe.js'
test('Falcon touch coordinates and intentional horizontal swipes',()=>{
  assert.deepEqual(touchPoint({changedTouches:[{pageX:500,pageY:140}]}),{x:500,y:140})
  assert.equal(touchPoint({}),null)
  assert.equal(swipeStep({x:500,y:140},{x:300,y:143}),1)
  assert.equal(swipeStep({x:300,y:140},{x:500,y:143}),-1)
  assert.equal(swipeStep({x:500,y:140},{x:480,y:145}),0)
  assert.equal(swipeStep({x:500,y:140},{x:490,y:250}),0)
  assert.equal(swipeStep({x:100,y:500},{x:103,y:300},true),1)
})
