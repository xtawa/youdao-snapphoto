import test from 'node:test'
import assert from 'node:assert/strict'
import { CameraBridge, createCameraBridge, CameraUnavailableError } from '../src/platform/camera-bridge.js'

test('no silent fake camera when native JSAPI is absent', async () => {
  const bridge = createCameraBridge({})
  await assert.rejects(bridge.connect(), CameraUnavailableError)
})
test('capabilities, zoom clamp and normalized focus', async () => {
  const log = []
  const native = {
    getCapabilities: () => ({camera:true, photo:true, preview:true, zoom:true, focus:true, minZoom:1, maxZoom:4, filters:['none'], gallery:true, share:true}),
    openPreview: () => {},
    getPreviewFrame: () => ({image:'data:image/jpeg;base64,YQ=='}),
    setZoom: value => log.push(['zoom', value]),
    focusAt: (x,y) => log.push(['focus',x,y])
  }
  const b = new CameraBridge(native)
  const caps = await b.connect()
  assert.equal(caps.maxZoom, 4)
  await b.openPreview()
  assert.equal(await b.getPreviewFrame(), 'data:image/jpeg;base64,YQ==')
  assert.equal(await b.setZoom(99),4)
  await b.focusAt(-1,2)
  assert.deepEqual(log,[['zoom',4],['focus',0,1]])
})
test('photo, recording and share lifecycle', async () => {
  let stopped = 0
  const b = new CameraBridge({
    getCapabilities: () => ({camera:true, photo:true, video:true, preview:true, gallery:true, share:true}),
    capturePhoto: () => ({path:'/userdisk/SnapPhoto/a.jpg'}),
    startRecording: () => {},
    stopRecording: () => ({path:'/userdisk/SnapPhoto/a.mp4'}),
    listMedia: () => [{path:'/userdisk/SnapPhoto/a.jpg'}],
    startShare: () => ({url:'http://192.168.1.100:8199/s/aabb'}),
    stopShare: () => {stopped++}
  })
  await b.connect()
  assert.equal((await b.capturePhoto()).path,'/userdisk/SnapPhoto/a.jpg')
  await b.startRecording()
  await assert.rejects(b.capturePhoto(),/录像期间/)
  assert.equal((await b.stopRecording()).path,'/userdisk/SnapPhoto/a.mp4')
  assert.equal((await b.listMedia()).length,1)
  assert.ok((await b.startShare(['/userdisk/SnapPhoto/a.jpg'])).url)
  await b.close()
  assert.equal(stopped,1)
})
test('rejects public URL and unsupported filter', async () => {
  const b = new CameraBridge({
    getCapabilities: () => ({camera:true, photo:true, share:true}),
    startShare: () => ({url:'http://203.0.113.10:8080/file'})
  })
  await b.connect()
  await assert.rejects(b.startShare(['x']),/局域网/)
  await assert.rejects(b.setFilter('mono'),CameraUnavailableError)
})
