import {GALLERY_DIRECTORY,galleryResult,validateGalleryPublication,safeAbsolutePath,withinDirectory} from './gallery-policy.js'
import {chooseCaptureProcessing,normalizeProcessingCapabilities} from './processing-policy.js'
/**
 * SnapPhoto native adapter contract.
 *
 * This file intentionally does not fake hardware support.
 * Youdao/Falcon camera methods have NOT been publicly verified.
 * See docs/native-bridge.md for required native module methods.
 */
export class CameraUnavailableError extends Error {
  constructor(detail) {
    super(detail || '未检测到 SnapPhoto 相机原生桥接模块')
    this.name = 'CameraUnavailableError'
  }
}
export function clamp(value, low, high) {
  const n = Number(value)
  if (!Number.isFinite(n)) return low
  return Math.min(high, Math.max(low, n))
}
export function resolveNative(root) {
  const scope = root || (typeof globalThis !== 'undefined' ? globalThis : {})
  const api = scope.$falcon && scope.$falcon.jsapi
  const possibilities = [scope.SnapPhotoNative, api && api.snapphoto]
  for (let i = 0; i < possibilities.length; i++) {
    const item = possibilities[i]
    if (item && typeof item.getCapabilities === 'function') return item
  }
  return null
}
export function normalizePhotoProfiles(list) {
  if (!Array.isArray(list)) return []
  const out = []
  for (let i = 0; i < list.length; i++) {
    const p = list[i]
    const w = Number(p && p.width), h = Number(p && p.height)
    const id = String(p && p.id || '')
    if (id && w > 0 && h > 0 && w >= h) out.push({ id, width: w, height: h })
  }
  return out
}
export class CameraBridge {
  constructor(native) {
    this.native = native || null
    this.caps = null
    this.recording = false
    this.previewOpen = false
    this.frameInfo = null
    this.lastFrameImage = null
    this.share = null
  }
  async connect() {
    if (!this.native) throw new CameraUnavailableError()
    const caps = await this.native.getCapabilities()
    if (!caps || caps.camera !== true) {
      throw new CameraUnavailableError('未检测到可用摄像头或摄像头权限不足')
    }
    const processing = normalizeProcessingCapabilities(caps)
    const hdrReady = processing.hdr && typeof this.native.captureHDR === 'function'
    const enhancementReady = processing.enhancement && typeof this.native.enhancePhoto === 'function'
    this.caps = {
      ...processing,
      hdr: hdrReady,
      enhancement: enhancementReady,
      npuReady: enhancementReady && processing.npuReady,
      camera: true,
      photo: caps.photo === true,
      video: caps.video === true,
      preview: caps.preview === true,
      zoom: caps.zoom === true,
      focus: caps.focus === true,
      continuousFocus: caps.continuousFocus === true,
      zeroShutterLag: caps.zeroShutterLag === true,
      backgroundEnhancement: caps.backgroundEnhancement===true && typeof this.native.queueEnhancement==='function',
      audio: caps.audio === true,
      filters: Array.isArray(caps.filters) ? caps.filters : ['none'],
      share: caps.share === true,
      gallery: caps.gallery === true,
      minZoom: clamp(caps.minZoom || 1, 1, 100),
      maxZoom: clamp(caps.maxZoom || 1, 1, 100),
      videoProfiles: Array.isArray(caps.videoProfiles)?caps.videoProfiles.filter(p=>p&&p.id&&p.width>0&&p.height>0&&p.portraitWidth>0&&p.portraitHeight>0).map(p=>({...p})):[],
      photoProfiles: normalizePhotoProfiles(caps.photoProfiles)
    }
    if (this.caps.maxZoom < this.caps.minZoom) this.caps.maxZoom = this.caps.minZoom
    return this.caps
  }
  requireCapability(flag) {
    if (!this.caps || !this.caps[flag]) throw new CameraUnavailableError('设备未提供功能：' + flag)
  }
  async openPreview() {
    this.requireCapability('preview')
    await this.native.openPreview()
    this.lastFrameImage = null
    this.frameInfo = null
    this.previewOpen = true
  }
  async getPreviewFrame() {
    if (!this.previewOpen) return null
    const data = await this.native.getPreviewFrame()
    this.frameInfo = data
    if (!data || typeof data.image !== 'string' || data.image === this.lastFrameImage) return null
    this.lastFrameImage = data.image
    return data.image
  }
  async capturePhoto(options) {
    this.requireCapability('photo')
    if (this.recording) throw new Error('录像期间不能拍照')
    const settings = options || {}
    const processing = chooseCaptureProcessing(settings, this.caps)
    if (!this.caps.zeroShutterLag && this.caps.continuousFocus && typeof this.native.waitForAutoFocus === 'function' && settings.settleFocus !== false) {
      this.lastFocus = await this.native.waitForAutoFocus()
      if (!this.previewOpen) throw new Error('相机已关闭')
      if (!this.lastFocus || this.lastFocus.settled !== true) throw new Error('焦点尚未稳定，请保持词典笔不动后重试')
    }
    const photo = processing.hdr !== 'off'
      ? await this.native.captureHDR({...settings,hdrMode:processing.hdr,exposuresEV:processing.exposuresEV,saveDirectory:GALLERY_DIRECTORY})
      : await this.native.capturePhoto({...settings,saveDirectory:GALLERY_DIRECTORY})
    if (!photo || !safeAbsolutePath(photo.path)) throw new Error('原生相机未返回合法的照片路径')
    let result = {...photo,hdrApplied:photo.hdrApplied===true,hdrBackend:photo.hdrBackend||'none'}
    if (processing.hdr==='on' && !result.hdrApplied) result.processingWarning='设备未完成 HDR 合成；保留原片'
    if(processing.denoise!=='off'&&this.caps.backgroundEnhancement){
      const published=await this.publishCapturedPhoto(result)
      try{
        const job=await this.native.queueEnhancement({sourcePath:result.path,mode:processing.denoise,capturedAtEpochMs:result.capturedAtEpochMs,keepAlive:settings.processingKeepAlive!==false})
        if(!job||job.queued!==true)throw new Error('Task not queued')
        return {...published,enhancementQueued:true,processingJobId:job.jobId}
      }catch(error){return {...published,galleryWarning:[published.galleryWarning,'后台降噪排队失败，原片已保存'].filter(Boolean).join('；')}}
    }
    if (processing.denoise!=='off') {
      try {
        const enhanced=await this.native.enhancePhoto({sourcePath:result.path,mode:processing.denoise,
          preferNpu:processing.preferNpu,saveDirectory:GALLERY_DIRECTORY,preserveOriginal:true,
          capturedAtEpochMs:result.capturedAtEpochMs,onProcessing:settings.onProcessing})
        if (enhanced && enhanced.written===true && safeAbsolutePath(enhanced.path)) {
          const changed=enhanced.path!==result.path
          result={...result,path:enhanced.path,indexed:changed?enhanced.indexed===true:result.indexed,
            denoiseApplied:enhanced.denoiseApplied===true,npuUsed:processing.preferNpu && enhanced.npuUsed===true,
            size:enhanced.size||result.size,npuTimeMs:enhanced.npuTimeMs,processingMs:enhanced.processingMs,tiles:enhanced.tiles}
        } else result.processingWarning=[result.processingWarning,'图像增强未完成，保留原片'].filter(Boolean).join('；')
      } catch(error) {
        result.processingWarning=[result.processingWarning,'图像增强失败，保留原片'].filter(Boolean).join('；')
      }
    }
    const published=await this.publishCapturedPhoto(result)
    if(result.processingWarning)published.galleryWarning=[published.galleryWarning,result.processingWarning].filter(Boolean).join('；')
    return published
  }
  async publishCapturedPhoto(result){
    if(this.caps.zeroShutterLag&&result.written===true&&withinDirectory(result.path,GALLERY_DIRECTORY)){
      this.saveToGallery(result,'photo').then(receipt=>{this.lastPublication=receipt}).catch(()=>{})
      return {...galleryResult(result),galleryWarning:'',publicationPending:true}
    }
    return this.saveToGallery(result,'photo')
  }
  async startRecording(options) {
    this.requireCapability('video')
    if (this.recording) throw new Error('正在录像')
    const result = await this.native.startRecording({...options, saveDirectory: GALLERY_DIRECTORY})
    this.recording = true
    if (this.caps.audio && (!result || result.audio !== true)) {
      try { await this.native.stopRecording() } finally { this.recording = false }
      throw new Error('麦克风未开始收音，录像已结束')
    }
    return result
  }
  async stopRecording() {
    if (!this.recording) throw new Error('当前没有录像')
    let result
    try { result = await this.native.stopRecording() } finally { this.recording = false }
    return this.saveToGallery(result, 'video')
  }
  async setZoom(value) {
    this.requireCapability('zoom')
    const zoom = clamp(value, this.caps.minZoom, this.caps.maxZoom)
    await this.native.setZoom(zoom)
    return zoom
  }
  async focusAt(x, y) {
    this.requireCapability('focus')
    return this.native.focusAt(clamp(x, 0, 1), clamp(y, 0, 1))
  }
  async setFilter(id) {
    this.requireCapability('camera')
    if (this.caps.filters.indexOf(id) < 0) throw new CameraUnavailableError('设备不支持滤镜：' + id)
    if (id !== 'none') {
      if (typeof this.native.setFilter !== 'function') throw new CameraUnavailableError('缺少滤镜原生接口')
      await this.native.setFilter(id)
    } else if (typeof this.native.setFilter === 'function') {
      await this.native.setFilter('none')
    }
    return id
  }
  async setCaptureMode(mode) {
    if (!['photo','video'].includes(mode)) throw new Error('未知拍摄模式')
    if (this.recording) throw new Error('请先结束录像')
    if (this.native && typeof this.native.setCaptureMode === 'function') await this.native.setCaptureMode(mode)
    return mode
  }
  async setPhotoProfile(id) {
    this.requireCapability('camera')
    if (!Array.isArray(this.caps.photoProfiles) || !this.caps.photoProfiles.length) return null
    const profile = this.caps.photoProfiles.find(p => p.id === id)
    if (!profile) throw new CameraUnavailableError('设备不支持该照片比例：' + id)
    if (typeof this.native.setPhotoProfile !== 'function') throw new CameraUnavailableError('缺少照片比例原生接口')
    await this.native.setPhotoProfile(id)
    return profile
  }
  async setVideoProfile(id) {
    this.requireCapability('video')
    if (this.recording) throw new Error('请先结束录像')
    if (!this.caps.videoProfiles.length) return null
    const profile = this.caps.videoProfiles.find(p=>p.id===id)
    if (!profile || typeof this.native.setVideoProfile !== 'function') throw new CameraUnavailableError('设备不支持该录像画质')
    await this.native.setVideoProfile(id)
    return profile
  }
  async saveToGallery(result, mediaType) {
    const saved = galleryResult(result)
    if (saved.indexed) return saved
    if (!this.native || typeof this.native.publishToGallery !== 'function') return saved
    try {
      const published = await this.native.publishToGallery({
        sourcePath: saved.path,
        targetDirectory: GALLERY_DIRECTORY,
        mediaType
      })
      return validateGalleryPublication(published)
    } catch (error) {
      return {
        ...saved,
        galleryWarning: '文件保留在 ' + saved.path + '；导入 /userdisk/Pictures 失败：' + (error && error.message || error)
      }
    }
  }
  async listMedia() {
    this.requireCapability('gallery')
    const files = await this.native.listMedia({directory: GALLERY_DIRECTORY, includeFallback: true})
    return Array.isArray(files) ? files : []
  }
  async startShare(paths) {
    this.requireCapability('share')
    if (!Array.isArray(paths) || !paths.length) throw new Error('请选择要分享的文件')
    // Native side MUST validate paths, bind LAN only and issue an expiring token.
    const result = await this.native.startShare(paths)
    if (!result || !/^http:\/\/(?:192\.168\.|10\.|172\.(?:1[6-9]|2[0-9]|3[01])\.)/.test(result.url || '')) {
      if (typeof this.native.stopShare === 'function') {
        try { await this.native.stopShare() } catch (error) {}
      }
      throw new Error('未获得有效的局域网分享地址')
    }
    this.share = result
    return result
  }
  async stopShare() {
    if (this.share && this.native && typeof this.native.stopShare === 'function') {
      try { await this.native.stopShare() } finally { this.share = null }
    } else {
      this.share = null
    }
  }
  async closePreviewForPlayback(){
    if(this.recording)throw new Error('请先结束录像')
    if(this.previewOpen){await this.native.closePreview();this.previewOpen=false;this.lastFrameImage=null}
  }
  async close() {
    await this.stopShare()
    if (this.recording) {
      try { await this.stopRecording() } catch (error) { this.recording = false }
    }
    if (this.previewOpen && typeof this.native.closePreview === 'function') {
      await this.native.closePreview()
    }
    this.previewOpen = false
  }
}
export function createCameraBridge(root) {
  return new CameraBridge(resolveNative(root))
}
