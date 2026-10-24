import test from 'node:test'
import assert from 'node:assert/strict'
import page from '../src/pages/index/index.js'
import {renderDeviceUI} from '../src/pages/index/device-ui.js'
function vm(){const v=page.data();for(const [k,f] of Object.entries(page.methods))v[k]=f.bind(v);return v}
test('selection opens no preview and the delete prompt performs no deletion',async()=>{
  const v=vm();v.selecting=true;let removed=0
  v.bridge={native:{deleteMedia:()=>{removed++}}}
  await v.openMedia({path:'/userdisk/Pictures/a.JPG',type:'photo'})
  assert.deepEqual(v.selected,['/userdisk/Pictures/a.JPG']);assert.equal(v.panel,'camera')
  v.requestDelete();assert.equal(v.deleteConfirm,true);assert.equal(removed,0)
  v.deleteConfirm=false;assert.equal(removed,0)
})
test('all-selection toggles and preserves selections outside the filtered view',()=>{
  const v=vm();v.visibleMedia=[{path:'a'},{path:'b'}];v.selected=['other']
  v.selectAllVisible();assert.deepEqual(v.selected,['other','a','b'])
  v.selectAllVisible();assert.deepEqual(v.selected,['other'])
})
test('media navigation stops old playback and does not wrap beyond the album',async()=>{
  const v=vm();v.visibleMedia=[{path:'a',type:'photo'},{path:'b',type:'video'}];v.mediaItem=v.visibleMedia[0];let stopped=0
  v.stopPlayback=async()=>{stopped++};await v.mediaChange(1)
  assert.equal(v.mediaItem.path,'b');assert.equal(stopped,1)
  await v.mediaChange(1);assert.equal(stopped,1)
})
test('stock-style gallery viewport fits portrait and landscape and exposes vertical scroll',()=>{
  const h=(tag,data,children)=>({tag,...data,children})
  for(const portrait of [false,true]){
    const v={...vm(),orientation:portrait?'portrait':'landscape',screenStyle:{},panel:'gallery',visibleMedia:Array.from({length:100},(_,i)=>({path:String(i),type:'photo'})),galleryRow:0}
    const tree=renderDeviceUI.call(v,h),scroller=tree.children.find(n=>n.tag==='scroller')
    assert.equal(scroller.attrs.scrollDirection,'vertical')
    assert.ok(parseFloat(scroller.style.left)+parseFloat(scroller.style.width)<=(portrait?280:936))
    assert.ok(parseFloat(scroller.style.top)+parseFloat(scroller.style.height)<=(portrait?936:280))
    assert.equal(scroller.children[0].children.length,100) // Stable scroll extent beyond tile 20.
    assert.ok(scroller.children[0].children.flatMap(n=>n.children).filter(n=>n.tag==='img').length<30)
  }
})
test('video frame updates reuse the native image view instead of replacing it with black',()=>{
  const h=(tag,data,children)=>({tag,...data,children}),v={...vm(),orientation:'landscape',screenStyle:{},panel:'media',mediaItem:{path:'clip.avi',type:'video'},mediaPreview:'file:///frame-1.jpg'}
  const first=renderDeviceUI.call(v,h).children.find(n=>n.tag==='img')
  v.mediaPreview='file:///frame-2.jpg'
  const second=renderDeviceUI.call(v,h).children.find(n=>n.tag==='img')
  assert.equal(first.key,second.key);assert.notEqual(first.attrs.src,second.attrs.src)
})
