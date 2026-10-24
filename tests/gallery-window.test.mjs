import test from 'node:test'
import assert from 'node:assert/strict'
import {galleryWindow,scrollOffset} from '../src/utils/gallery-window.js'
import page from '../src/pages/index/index.js'
import {renderDeviceUI} from '../src/pages/index/device-ui.js'
function vm(count=180){
  const v=page.data();for(const [key,fn] of Object.entries(page.methods))v[key]=fn.bind(v)
  Object.defineProperty(v,'orientation',{get:()=>page.computed.orientation.call(v)})
  Object.defineProperty(v,'visibleMedia',{get:()=>page.computed.visibleMedia.call(v)})
  v.media=Array.from({length:count},(_,i)=>({path:'photo-'+i+'.JPG',type:'photo',size:123,modified:1}))
  v.panel='gallery';v._visible=true;return v
}
test('native/wrapped scroll offsets, negative positions, numeric strings and malformed events',()=>{
  for(const event of [{y:-984},{y:'-984px'},{contentOffset:{y:-984}},{detail:{y:-984}},JSON.stringify({y:-984}),{scrollTop:984}])assert.equal(scrollOffset(event),984)
  for(const event of [null,{},'bad',{y:NaN},{y:Infinity}])assert.equal(scrollOffset(event),null)
})
test('windows cover the last photo in large albums in both directions without a 20-item limit',()=>{
  for(const orientation of ['landscape','portrait'])for(const selecting of [true,false]){
    const layout=galleryWindow(1001,orientation,9999,selecting)
    assert.equal(layout.end,1001);assert.ok(layout.start>20)
    assert.ok(layout.totalHeight>layout.height)
    assert.ok(layout.end-layout.start<30)
  }
})
test('scrolling past tile 20 mounts the later cached images with stable content extent',()=>{
  const v=vm(180);v.media=v.media.map(f=>({...f,thumbnail:'/tmp/'+f.path}))
  v.loadThumbnails=()=>{};v.galleryScroll({detail:{y:-164*12}})
  assert.equal(v.galleryRow,12)
  const h=(tag,data,children)=>({tag,...data,children})
  const tree=renderDeviceUI.call(v,h),content=tree.children.find(n=>n.tag==='scroller').children[0]
  assert.equal(content.children.length,180)
  const images=content.children.flatMap(n=>n.children).filter(n=>n.tag==='img')
  assert.ok(images.some(n=>n.attrs.src.endsWith('photo-60.JPG')))
  assert.ok(images.every(n=>!n.attrs.src.endsWith('photo-0.JPG')))
  assert.ok(images.length<30)
})
test('a fast scroll replans one worker and keeps an in-flight completed thumbnail',async()=>{
  const v=vm();let release,active=0,peak=0;const calls=[]
  v.bridge={native:{thumbnail:async path=>{
    calls.push(path);active++;peak=Math.max(peak,active)
    if(calls.length===1)await new Promise(resolve=>{release=resolve})
    active--;return {path:'/tmp/'+path}
  }}}
  const work=v.loadThumbnails();assert.equal(calls.length,1)
  v.galleryScroll({y:-164*14});v.galleryScroll({y:-164*15})
  release();await work
  assert.equal(peak,1);assert.equal(calls[1],'photo-75.JPG')
  assert.ok(v.media[0].thumbnail);assert.ok(v.media[75].thumbnail)
  assert.ok(!calls.includes('photo-1.JPG'));assert.ok(calls.length<30)
})
test('gallery refresh preserves valid thumbnails and drops changed-file thumbnails',async()=>{
  const v=vm(2);v.caps={gallery:true};v.media[0].thumbnail='cached-0';v.media[1].thumbnail='cached-1'
  v.bridge={listMedia:async()=>[{...v.media[0],thumbnail:undefined},{...v.media[1],thumbnail:undefined,size:999}]}
  await v.refreshGallery();assert.equal(v.media[0].thumbnail,'cached-0');assert.equal(v.media[1].thumbnail,undefined)
})
test('thumbnail worker stops when the page hides and a failed image does not block later tiles',async()=>{
  const v=vm(50);const calls=[]
  v.bridge={native:{thumbnail:async path=>{
    calls.push(path);if(calls.length===1)throw Error('bad JPEG')
    if(calls.length===3)v._visible=false
    return {path:'/tmp/'+path}
  }}}
  await v.loadThumbnails();assert.equal(calls.length,3);assert.ok(v.media[1].thumbnail);assert.equal(v._thumbnailWork,null)
})
test('selection changes keep the existing scroller and media return resets its window',async()=>{
  const v=vm(90);v.galleryRow=10
  const h=(tag,data,children)=>({tag,...data,children})
  const key=()=>renderDeviceUI.call(v,h).children.find(n=>n.tag==='scroller').key
  const first=key();v.selecting=true;assert.equal(key(),first)
  v.stopPlayback=async()=>{};v.loadThumbnails=()=>{};await v.leaveMedia();assert.equal(v.galleryRow,0)
})
test('a resumed camera bridge starts a fresh gallery worker after the old request resolves',async()=>{
  const v=vm(10);let release
  v.bridge={native:{thumbnail:()=>new Promise(resolve=>{release=()=>resolve({path:'old-frame'})})}}
  const old=v.loadThumbnails()
  v.bridge={native:{thumbnail:async path=>({path:'new-'+path})}}
  v.loadThumbnails();release();await old
  if(v._thumbnailWork)await v._thumbnailWork
  assert.ok(v.media.every(file=>file.thumbnail==='new-'+file.path))
})
test('gallery releases the camera before decoding thumbnails and camera return reopens it',async()=>{
  const v=vm(10),events=[];v.panel='camera';v.preview='old.jpg'
  v.bridge={previewOpen:true,closePreviewForPlayback:async()=>{events.push('pause');v.bridge.previewOpen=false},openPreview:async()=>{events.push('open');v.bridge.previewOpen=true},native:{}}
  v.refreshGallery=async()=>{events.push('list')};v.loadThumbnails=()=>{events.push('thumbnails')};v.poll=()=>{events.push('poll')};v.stopPlayback=async()=>{}
  await v.gallery();assert.equal(v.panel,'gallery');assert.equal(v.preview,'');assert.equal(v.bridge.previewOpen,false)
  assert.deepEqual(events,['pause','list','thumbnails']);await v.returnCamera();assert.equal(v.panel,'camera');assert.equal(v.bridge.previewOpen,true)
  assert.deepEqual(events,['pause','list','thumbnails','open','poll'])
})
