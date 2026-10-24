import test from 'node:test'
import assert from 'node:assert/strict'
import component from '../src/pages/index/index.js'
import {renderDeviceUI} from '../src/pages/index/device-ui.js'
const deferred=()=>{let resolve;const promise=new Promise(r=>resolve=r);return {promise,resolve}}
function vm(){const v=component.data();for(const [name,fn] of Object.entries(component.methods))v[name]=fn.bind(v);return v}
test('late frame from hidden camera cannot replace the resumed preview',async()=>{
  const v=vm(),frame=deferred(),requested=deferred()
  v._visible=true;v.connected=true;v.sessionId=10
  v.bridge={getPreviewFrame(){requested.resolve();return frame.promise},async close(){}}
  v.poll();await requested.promise
  await v.onHide();v._visible=true;v.sessionId++;v.preview='new-session.jpg'
  frame.resolve('old-session.jpg');await Promise.resolve();await Promise.resolve()
  assert.equal(v.preview,'new-session.jpg')
})
test('hidden onShow cannot reopen camera after settings finish loading',async()=>{
  const v=vm(),settings=deferred();let opened=0
  v.restoreSettings=()=>settings.promise;v.connect=async()=>{opened++};v.updateOrientation=()=>{}
  const shown=v.onShow();await v.onHide();settings.resolve();await shown
  assert.equal(opened,0);assert.equal(v.orientationTimer,null)
})
test('portrait preview gets a fresh native image when its camera session changes',()=>{
  function key(sessionId){const v={orientation:'portrait',sessionId,screenStyle:{},panel:'camera',preview:'frame.jpg',mode:'photo',zoom:1,selected:[],showGrid:false,countdown:0,toast:'',timerDelay:0,burstCount:1};const h=(tag,data,children)=>({tag,data,children});return renderDeviceUI.call(v,h).children.find(n=>n.tag==='img').data.key}
  assert.notEqual(key(1),key(2))
})
