import test from 'node:test'
import assert from 'node:assert/strict'
import {chooseCaptureProcessing,normalizeProcessingCapabilities,processingLabel,HDR_EXPOSURES_EV} from '../src/platform/processing-policy.js'
test('never claims NPU from hardware name or unverified flags',()=>{
  const c=normalizeProcessingCapabilities({npu:true,npuReady:false,enhancement:true,hdr:true,hdrStrategies:['bracket']})
  assert.equal(c.npuReady,false)
  const capture=chooseCaptureProcessing({hdrMode:'auto',denoiseMode:'auto'},c)
  assert.equal(capture.preferNpu,false)
  assert.deepEqual(capture.exposuresEV,[-2,0,2])
})
test('HDR is disabled without actual ISP or bracket capability',()=>{
  const p=chooseCaptureProcessing({hdrMode:'auto',denoiseMode:'auto'}, {})
  assert.equal(p.hdr,'off')
  assert.equal(p.denoise,'off')
  assert.deepEqual(p.exposuresEV,[])
  assert.throws(()=>chooseCaptureProcessing({hdrMode:'on'},{}),/HDR/)
})
test('enforce enhancement capability for quality',()=>{
  assert.throws(()=>chooseCaptureProcessing({denoiseMode:'quality'},{enhancement:false}),/图像增强/)
})
test('only verified ready NPU enables preference flag',()=>{
  const p=chooseCaptureProcessing({hdrMode:'on',denoiseMode:'quality'},{
    hdr:true,hdrStrategies:['isp'],enhancement:true,npu:true,npuReady:true,npuBackend:'rknn'
  })
  assert.equal(p.hdr,'on')
  assert.equal(p.denoise,'quality')
  assert.equal(p.preferNpu,true)
  assert.deepEqual(p.exposuresEV,[])
  assert.deepEqual(HDR_EXPOSURES_EV,[-2,0,2])
})
test('status label reflects actual returned result, not requested config',()=>{
  assert.equal(processingLabel({hdrApplied:false,npuUsed:false}),'普通拍照')
  assert.equal(processingLabel({hdrApplied:true,hdrBackend:'bracket',npuUsed:true}),'HDR 多帧 · NPU 增强')
})
