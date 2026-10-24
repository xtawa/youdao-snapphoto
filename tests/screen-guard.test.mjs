import test from 'node:test'
import assert from 'node:assert/strict'
import {createScreenGuard} from '../src/platform/screen-guard.js'
test('foreground screen request pairs once across repeated lifecycle callbacks',()=>{
  const calls=[]
  const g=createScreenGuard({startAlwaysScreenOn(){calls.push('start')},stopAlwaysScreenOn(){calls.push('stop')}})
  g.start();g.start();g.stop();g.stop();g.start();g.stop()
  assert.deepEqual(calls,['start','stop','start','stop'])
})
test('missing paired screen API cannot acquire a request that cannot be released',()=>{
  let started=0
  const g=createScreenGuard({startAlwaysScreenOn(){started++}})
  assert.equal(g.start(),false);g.stop();assert.equal(started,0)
})
