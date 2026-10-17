import {GALLERY_DIRECTORY,galleryResult,validateGalleryPublication} from './gallery-policy.js'
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
export class CameraBridge {
  constructor(native) {
    this.native = native || null
    this.caps = null
    this.recording = false
    this.previewOpen = false
    this.share = null
  }
  async connect() {
    if (!this.native) throw new CameraUnavailableError()
    const caps = await this.native.getCapabilities()
    if (!caps || caps.camera !== true) {
      throw new CameraUnavailableError('未检测到可用摄像头或摄像头权限不足')
    }
    this.caps = {
      camera: true,
      photo: caps.photo === true,
      video: caps.video === true,
      preview: caps.preview === true,
      zoom: caps.zoom === true,
      focus: caps.focus === true,
      filters: Array.isArray(caps.filters) ? caps.filters : ['none'],
      share: caps.share === true,
      gallery: caps.gallery === true,
      minZoom: clamp(caps.minZoom || 1, 1, 100),
      maxZoom: clamp(caps.maxZoom || 1, 1, 100)
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
    this.previewOpen = true
  }
  async getPreviewFrame() {
    if (!this.previewOpen) return null
    const data = await this.native.getPreviewFrame()
    return data && typeof data.image === 'string' ? data.image : null
  }
  async capturePhoto(options) {
    this.requireCapability('photo')
    if (this.recording) throw new Error('录像期间不能拍照')
    const result = await this.native.capturePhoto({...options, saveDirectory: GALLERY_DIRECTORY})
    return this.saveToGallery(result, 'photo')
  }
  async startRecording(options) {
    this.requireCapability('video')
    if (this.recording) throw new Error('正在录像')
    await this.native.startRecording({...options, saveDirectory: GALLERY_DIRECTORY})
    this.recording = true
  }
  async stopRecording() {
    if (!this.recording) throw new Error('当前没有录像')
    const result = await this.native.stopRecording()
    this.recording = false
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
  async saveToGallery(result, mediaType) {
    const saved = galleryResult(result)
    if (saved.album === 'pictures') return saved
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
