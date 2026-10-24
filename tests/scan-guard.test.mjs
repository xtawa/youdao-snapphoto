import test from 'node:test'
import assert from 'node:assert/strict'
import {createScanGuard} from '../src/platform/scan-guard.js'

test('foreground scan guard consumes only its app head events and releases on background',()=>{
  const calls=[],appid='8001953602710246'
  const guard=createScanGuard({addUseOcrAppId:value=>calls.push(['add',JSON.parse(value)]),deleteUseOcrAppId:value=>calls.push(['delete',value])},appid)
  guard.stop();guard.start();guard.start();guard.stop();guard.stop();guard.start();guard.stop()
  assert.deepEqual(calls,[['add',{appid}],['delete',appid],['add',{appid}],['delete',appid]])
})
test('failed registration is not marked active and incomplete APIs fail without partial state',()=>{
  let count=0
  const guard=createScanGuard({addUseOcrAppId(){count++;throw new Error('failed')},deleteUseOcrAppId(){throw new Error('unexpected cleanup')}},'app')
  assert.throws(()=>guard.start(),/failed/);guard.stop();assert.throws(()=>guard.start(),/failed/);assert.equal(count,2)
  assert.throws(()=>createScanGuard({addUseOcrAppId(){throw new Error('should not register')}},'app').start(),/系统扫描拦截接口不可用/)
})
