import photoAlbum from 'photoAlbum'
import {createStockAlbum,stockImageRows} from './stock-album.js'
import native from 'snapphoto'
import * as gsensorModule from 'gsensor'
import {createOrientationSensor} from './orientation-sensor.js'
import nativeGlobal from 'global'
import {createScanGuard} from './scan-guard.js'
import {createScreenGuard} from './screen-guard.js'
const orientationSensor=createOrientationSensor(gsensorModule,()=>call('sensor'))
const stockAlbum=createStockAlbum(new photoAlbum.PhotoAlbum())
let thumbnailQueue=Promise.resolve()
let scanGuard=null,screenGuard=null
export function startScreenGuard(){if(!screenGuard)screenGuard=createScreenGuard(new nativeGlobal.Global());console.warn('[snapphoto] screen guard '+screenGuard.start())}
export function stopScreenGuard(){if(screenGuard)screenGuard.stop()}
export function startScanGuard(){
  if(!scanGuard)scanGuard=createScanGuard(new nativeGlobal.Global(),'8001953602710246')
  scanGuard.start()
  console.warn('[snapphoto] foreground scan guard enabled')
}
export function stopScanGuard(){
  if(scanGuard){scanGuard.stop();console.warn('[snapphoto] foreground scan guard released')}
}

export function call(op, args) {
  const result = JSON.parse(native.command(JSON.stringify({op, ...args})))
  if (result && result.error) throw new Error(result.error)
  return result
}
export function installX7ProNative() {
  globalThis.SnapPhotoNative = {
    getCapabilities: () => call('caps'),
    openPreview: () => call('open'),
    getPreviewFrame: () => {const frame=call('frame');return frame.image?{...frame,image:'file://'+frame.image}:frame},
    closePreview: () => call('close'),
    queueEnhancement: options => call('queueEnhance',{path:options.sourcePath,mode:options.mode,capturedAtEpochMs:options.capturedAtEpochMs,keepAlive:options.keepAlive}),
    getProcessingStatus: () => call('queueStatus'),
    resumeProcessing: () => call('queueResume'),
    publishProcessing: async jobs=>{
      const complete=jobs.filter(job=>job.state==='done'&&!job.published)
      if(!complete.length)return 0
      for(const job of complete){if(job.result&&job.result.denoiseApplied){try{call('publish',{path:job.result.path})}catch(e){}}}
      await stockAlbum.scan()
      for(const job of complete)call('queueAck',{id:job.id})
      return complete.filter(job=>job.result&&job.result.denoiseApplied).length
    },
    capturePhoto: (options={}) => new Promise((resolve,reject)=>{
      try{call('photoStart',{fast:options.settleFocus===false})}catch(e){reject(e);return}
      const poll=()=>{try{const result=call('photoResult');if(result.pending)setTimeout(poll,25);else resolve(result)}catch(e){reject(e)}}
      setTimeout(poll,25)
    }),
    enhancePhoto: (options={}) => new Promise((resolve,reject)=>{
      try{call('enhanceStart',{path:options.sourcePath,mode:options.mode,capturedAtEpochMs:options.capturedAtEpochMs})}catch(e){reject(e);return}
      const poll=()=>{try{
        const result=call('enhanceResult')
        if(result.pending){if(typeof options.onProcessing==='function')options.onProcessing(result.progress);setTimeout(poll,150)}
        else resolve(result)
      }catch(e){reject(e)}}
      setTimeout(poll,150)
    }),
    waitForAutoFocus: () => new Promise((resolve,reject)=>{
      try{call('focusStart')}catch(e){reject(e);return}
      const poll=()=>{try{const result=call('focusResult');if(result.pending)setTimeout(poll,50);else resolve(result)}catch(e){reject(e)}}
      setTimeout(poll,50)
    }),
    startRecording: () => call('record'),
    stopRecording: () => call('stopRecord'),
    setZoom: value => call('zoom', {value}),
    setFilter: value => call('filter', {value}),
    setCaptureMode: value => call('captureMode', {value}),
    setPhotoProfile: value => call('photoProfile', {value}),
    setVideoProfile: value => call('videoProfile', {value}),
    setOrientation: (value,rotation=0) => call('orientation', {value,rotation}),
    setNight: (mode,priority) => call('night',{mode,priority}),
    publishToGallery: async ({sourcePath,mediaType})=>{
      if(mediaType==='video')return {path:sourcePath,written:true,indexed:false}
      const publication=call('publish',{path:sourcePath});sourcePath=publication.path
      await stockAlbum.scan()
      const rows=stockImageRows(await stockAlbum.request('loadImages',{sort_way:'created_time',order_type:'desc',last_key:'',page_size:256}))
      const indexed=rows.some(row=>row.local_path===sourcePath)
      console.warn('[snapphoto] album publication '+JSON.stringify({path:sourcePath,indexed}))
      return {path:sourcePath,written:true,indexed}
    },
    importMedia: async paths=>{const imported=[],failed=[];for(const path of paths){try{imported.push(call('publish',{path}).path)}catch(e){failed.push(path)}};await stockAlbum.scan();return {imported,failed}},
    deleteMedia: async paths=>{const r=call('delete',{paths});try{await stockAlbum.scan()}catch(e){};return r},
    thumbnail: path=>{const job=()=>new Promise((resolve,reject)=>{
      try{const r=call('thumbnailStart',{path});if(!r.pending){resolve(r);return}}catch(e){reject(e);return}
      const poll=()=>{try{const r=call('thumbnailResult');if(r.pending)setTimeout(poll,80);else resolve(r)}catch(e){reject(e)}};setTimeout(poll,80)
    });const result=thumbnailQueue.then(job,job);thumbnailQueue=result.catch(()=>{});return result},
    playMedia: (path,orientation)=>call('play',{path,orientation}),
    playbackFrame: ()=>call('playFrame'),
    stopPlayback: ()=>call('stopPlay'),
    persistQr: value => 'file://'+call('qr', {value}).path,
    listMedia: () => call('list').sort((a,b) => b.modified-a.modified),
    startShare: paths => call('share', {paths}),
    stopShare: () => call('stopShare'),
    getDisplaySize: () => ({width:936,height:280}),
    getOrientationSensor: () => orientationSensor.read(),
    startOrientationSensor: () => orientationSensor.start(),
    stopOrientationSensor: () => orientationSensor.stop(),
    readPreferences: () => call('readPrefs'),
    writePreferences: value => call('writePrefs', {value})
  }
}
