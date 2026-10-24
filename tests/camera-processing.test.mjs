import test from 'node:test'
import assert from 'node:assert/strict'
import {CameraBridge} from '../src/platform/camera-bridge.js'
test('no HDR without native captureHDR despite capability flag',async()=>{
  const b=new CameraBridge({
    getCapabilities:()=>({camera:true,photo:true,hdr:true,hdrStrategies:['isp']}),
    capturePhoto:()=>({path:'/userdisk/Pictures/plain.jpg'})
  })
  assert.equal((await b.connect()).hdr,false)
  await assert.rejects(b.capturePhoto({hdrMode:'on'}),/HDR/)
  assert.equal((await b.capturePhoto({hdrMode:'auto'})).hdrApplied,false)
})
test('genuine bracket and ready NPU are explicit native calls',async()=>{
  const calls=[]
  const b=new CameraBridge({
    getCapabilities:()=>({camera:true,photo:true,hdr:true,hdrStrategies:['bracket'],enhancement:true,npu:true,npuReady:true,npuBackend:'rknn'}),
    captureHDR:opts=>{calls.push(['hdr',opts]);return{path:'/userdisk/Pictures/hdr.jpg',hdrApplied:true,hdrBackend:'bracket'}},
    enhancePhoto:opts=>{calls.push(['enhance',opts]);return{path:'/userdisk/Pictures/denoised.jpg',written:true,denoiseApplied:true,npuUsed:true}}
  })
  const caps=await b.connect()
  assert.equal(caps.hdr,true)
  assert.equal(caps.npuReady,true)
  const result=await b.capturePhoto({hdrMode:'on',denoiseMode:'quality',zoom:1.5})
  assert.equal(calls[0][0],'hdr')
  assert.deepEqual(calls[0][1].exposuresEV,[-2,0,2])
  assert.equal(calls[0][1].saveDirectory,'/userdisk/Pictures')
  assert.equal(calls[1][1].preferNpu,true)
  assert.equal(result.path,'/userdisk/Pictures/denoised.jpg')
  assert.equal(result.hdrApplied,true)
  assert.equal(result.npuUsed,true)
})
test('auto HDR can skip fusion without false result',async()=>{
  const b=new CameraBridge({
    getCapabilities:()=>({camera:true,photo:true,hdr:true,hdrStrategies:['isp']}),
    captureHDR:()=>({path:'/userdisk/Pictures/original.jpg',hdrApplied:false})
  })
  await b.connect()
  const result=await b.capturePhoto({hdrMode:'auto'})
  assert.equal(result.hdrApplied,false)
  assert.equal(result.hdrBackend,'none')
})
test('enhancement failures preserve original and report warning',async()=>{
  const b=new CameraBridge({
    getCapabilities:()=>({camera:true,photo:true,enhancement:true,npu:true,npuReady:true}),
    capturePhoto:()=>({path:'/userdisk/Pictures/raw.jpg'}),
    enhancePhoto:()=>{throw new Error('NPU driver missing')}
  })
  await b.connect()
  const result=await b.capturePhoto({denoiseMode:'quality'})
  assert.equal(result.path,'/userdisk/Pictures/raw.jpg')
  assert.equal(result.npuUsed,undefined)
  assert.match(result.galleryWarning,/增强失败/)
})
test('unready NPU cannot be credited, even if service misreports use',async()=>{
  const b=new CameraBridge({
    getCapabilities:()=>({camera:true,photo:true,enhancement:true,npu:true,npuReady:false}),
    capturePhoto:()=>({path:'/userdisk/Pictures/raw.jpg'}),
    enhancePhoto:()=>({path:'/userdisk/Pictures/processed.jpg',written:true,denoiseApplied:true,npuUsed:true})
  })
  assert.equal((await b.connect()).npuReady,false)
  const result=await b.capturePhoto({denoiseMode:'auto'})
  assert.equal(result.npuUsed,false)
  assert.equal(result.denoiseApplied,true)
})
