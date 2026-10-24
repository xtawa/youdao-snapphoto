import test from 'node:test'
import assert from 'node:assert/strict'
import {previewRect} from '../src/utils/photo-layout.js'
import {PHOTO_PROFILES} from '../src/utils/format.js'
import page from '../src/pages/index/index.js'
import {renderDeviceUI} from '../src/pages/index/device-ui.js'

test('complete photo crop fits the display at every ratio without stretching',()=>{
  for(const profile of PHOTO_PROFILES){
    const v=previewRect('landscape','photo',PHOTO_PROFILES,profile.id)
    assert.ok(Math.abs(v.width/v.height-profile.width/profile.height)<1e-8)
    assert.ok(v.x>=0&&v.y>=0&&v.x+v.width<=936&&v.y+v.height<=280)
  }
  assert.deepEqual(previewRect('portrait','photo',PHOTO_PROFILES,'square'),{x:0,y:328,width:280,height:280})
  const video=previewRect('landscape','video',PHOTO_PROFILES,'square')
  assert.ok(Math.abs(video.width/video.height-640/192)<1e-8)
})
test('portrait ratio control never changes native state or stored preference',async()=>{
  let changed=false
  const vm={orientation:'portrait',message:()=>{},bridge:{setPhotoProfile:()=>{changed=true}},connected:true}
  await page.methods.applyPhotoProfile.call(vm,'square')
  assert.equal(changed,false)
})
test('ratio and export panels fit the 936 by 280 display',()=>{
  const h=(tag,data,children)=>({tag,...data,children})
  const noop=()=>{}
  const vm={orientation:'landscape',screenStyle:{},mode:'photo',size:()=> '1 MB',photoProfileList:PHOTO_PROFILES,photoProfile:'wide',chosenFiles:['/a.jpg'],media:[{path:'/a.jpg',size:100}],shareRemaining:'09:59',shareCountdown:599,qr:'file:///qr.png',shareUrl:'http://192.168.1.1:1234/'+'a'.repeat(48)+'/',message:noop,applyPhotoProfile:noop,share:noop,stopShare:noop}
  for(const panel of ['ratios','share']){
    const tree=renderDeviceUI.call({...vm,panel},h)
    for(const node of tree.children){
      const s=node.style;if(!s||s.position!=='absolute')continue
      assert.ok(parseFloat(s.left)>=0&&parseFloat(s.top)>=0)
      assert.ok(parseFloat(s.left)+parseFloat(s.width)<=936,panel+' overflow x')
      assert.ok(parseFloat(s.top)+parseFloat(s.height)<=280,panel+' overflow y')
    }
  }
})
test('share returned after backgrounding is stopped and cannot restore a stale URL',async()=>{
  let resolve,stopped=0
  const vm={...page.data(),_visible:true,sessionId:1,caps:{share:true},chosenFiles:['/a.jpg'],message:()=>{},bridge:{startShare:()=>new Promise(r=>{resolve=r}),stopShare:async()=>{stopped++}}}
  for(const [name,fn] of Object.entries(page.methods))vm[name]=fn.bind(vm)
  const pending=vm.share();vm._visible=false;vm.sessionId=2
  resolve({url:'http://192.168.1.1/test/',expiresIn:600});await pending
  assert.equal(vm.shareUrl,'');assert.equal(stopped,1)
})
