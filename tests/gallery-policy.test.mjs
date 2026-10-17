import test from 'node:test'
import assert from 'node:assert/strict'
import {GALLERY_DIRECTORY,APP_ALBUM_DIRECTORY,galleryResult,withinDirectory,validateGalleryPublication} from '../src/platform/gallery-policy.js'
import {CameraBridge} from '../src/platform/camera-bridge.js'

test('user-specified X7 Pro Pictures directory is the preferred target',()=>{
  assert.equal(GALLERY_DIRECTORY,'/userdisk/Pictures')
  assert.equal(APP_ALBUM_DIRECTORY,'/userdisk/SnapPhoto')
  assert.equal(withinDirectory('/userdisk/Pictures/p.jpg',GALLERY_DIRECTORY),true)
  assert.equal(withinDirectory('/userdisk/Pictures2/p.jpg',GALLERY_DIRECTORY),false)
  assert.equal(withinDirectory('/userdisk/Pictures/../private.jpg',GALLERY_DIRECTORY),false)
})
test('directly saved file is attributed to Pictures but index is not presumed',()=>{
  const saved=galleryResult({path:'/userdisk/Pictures/2026.jpg'})
  assert.equal(saved.album,'pictures')
  assert.equal(saved.indexed,false)
  assert.match(saved.galleryWarning,/索引/)
  assert.equal(galleryResult({path:'/userdisk/Pictures/2026.jpg',indexed:true}).galleryWarning,'')
})
test('publication receipt must confirm actual write in Pictures',()=>{
  assert.throws(()=>validateGalleryPublication({path:'/userdisk/Pictures/a.jpg',registered:true}),/失败/)
  assert.throws(()=>validateGalleryPublication({path:'/userdisk/Other/a.jpg',written:true}),/失败/)
  assert.equal(validateGalleryPublication({path:'/userdisk/Pictures/a.jpg',written:true,indexed:true}).indexed,true)
})
test('photo passes desired directory to native capture',async()=>{
  const captures=[]
  const bridge=new CameraBridge({
    getCapabilities:()=>({camera:true,photo:true}),
    capturePhoto:opts=>{captures.push(opts);return{path:'/userdisk/Pictures/a.jpg',indexed:true}}
  })
  await bridge.connect()
  const result=await bridge.capturePhoto({zoom:2})
  assert.equal(captures[0].saveDirectory,GALLERY_DIRECTORY)
  assert.equal(captures[0].zoom,2)
  assert.equal(result.album,'pictures')
  assert.equal(result.indexed,true)
})
test('photo publication copies fallback photo into preferred directory',async()=>{
  const copies=[]
  const bridge=new CameraBridge({
    getCapabilities:()=>({camera:true,photo:true}),
    capturePhoto:()=>({path:'/userdisk/SnapPhoto/a.jpg'}),
    publishToGallery:args=>{copies.push(args);return{written:true,indexed:true,path:'/userdisk/Pictures/a.jpg'}}
  })
  await bridge.connect()
  const result=await bridge.capturePhoto()
  assert.deepEqual(copies,[{sourcePath:'/userdisk/SnapPhoto/a.jpg',targetDirectory:'/userdisk/Pictures',mediaType:'photo'}])
  assert.equal(result.path,'/userdisk/Pictures/a.jpg')
  assert.equal(result.album,'pictures')
  assert.equal(result.indexed,true)
})
test('failed importer retains original path and exposes warning',async()=>{
  for(const publisher of [undefined,()=>{throw Error('filesystem readonly')},()=>({written:false,path:'/userdisk/Pictures/a.jpg'})]){
    const bridge=new CameraBridge({
      getCapabilities:()=>({camera:true,photo:true}),
      capturePhoto:()=>({path:'/userdisk/SnapPhoto/a.jpg'}),
      ...(publisher?{publishToGallery:publisher}:{})
    })
    await bridge.connect()
    const result=await bridge.capturePhoto()
    assert.equal(result.path,'/userdisk/SnapPhoto/a.jpg')
    assert.equal(result.album,'other')
    assert.ok(result.galleryWarning)
  }
})
test('videos request Pictures directory and publish with video media type',async()=>{
  let options,copied
  const bridge=new CameraBridge({
    getCapabilities:()=>({camera:true,video:true}),
    startRecording:opts=>{options=opts},
    stopRecording:()=>({path:'/userdisk/SnapPhoto/v.mp4'}),
    publishToGallery:args=>{copied=args;return{written:true,path:'/userdisk/Pictures/v.mp4'}}
  })
  await bridge.connect()
  await bridge.startRecording({filter:'none'})
  const result=await bridge.stopRecording()
  assert.equal(options.saveDirectory,GALLERY_DIRECTORY)
  assert.equal(copied.mediaType,'video')
  assert.equal(result.album,'pictures')
})
test('gallery lists from Pictures when native gallery support exists',async()=>{
  let opts
  const bridge=new CameraBridge({
    getCapabilities:()=>({camera:true,gallery:true}),
    listMedia:arg=>{opts=arg;return[{path:'/userdisk/Pictures/x.jpg'}]}
  })
  await bridge.connect()
  assert.equal((await bridge.listMedia()).length,1)
  assert.equal(opts.directory,GALLERY_DIRECTORY)
  assert.equal(opts.includeFallback,true)
})
