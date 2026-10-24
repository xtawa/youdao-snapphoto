import test from 'node:test'
import assert from 'node:assert/strict'
import {createStockAlbum,stockImageRows} from '../src/platform/stock-album.js'
import {normalizePreferences} from '../src/utils/preferences.js'
test('stock album requests serialize and unregister their result handler',async()=>{
  const listeners=new Set(),calls=[];let id=0
  const album=createStockAlbum({onRequestDataResult:{on:f=>listeners.add(f),off:f=>listeners.delete(f)},requestData:(endpoint,args)=>{
    const task=++id;calls.push(endpoint);setTimeout(()=>{for(const f of listeners)f(1,task,JSON.stringify({success:true}))},0);return task
  }})
  await Promise.all([album.scan(),album.scan()])
  assert.equal(listeners.size,0);assert.deepEqual(calls,['local/interface/scanImages','local/interface/scanImages'])
})
test('stock album failure does not mark a file as indexed',async()=>{
  let handler
  const album=createStockAlbum({onRequestDataResult:{on:f=>{handler=f},off:()=>{handler=null}},requestData:()=>{setTimeout(()=>handler(2,'bad',''),0);return 'bad'}})
  await assert.rejects(album.scan(),/请求失败/);assert.equal(handler,null)
})
test('stock result decoding preserves the verified local path and item ID columns',()=>{
  assert.deepEqual(stockImageRows({result:JSON.stringify({data:[['7','id','photo.JPG','/userdisk/Pictures/photo.JPG','/thumb.JPG']]})}),[{item_id:'id',name:'photo.JPG',local_path:'/userdisk/Pictures/photo.JPG',thumbnail_path:'/thumb.JPG'}])
})
test('night settings migrate safely and preserve an explicit off/quality choice',()=>{
  assert.equal(normalizePreferences({}).nightPriority,'speed')
  assert.equal(normalizePreferences({nightMode:'invalid'}).nightMode,'auto')
  const p=normalizePreferences({nightMode:'off',nightPriority:'quality'})
  assert.equal(p.nightMode,'off');assert.equal(p.nightPriority,'quality')
})
