/**
 * User-identified X7 Pro photo album directory.
 * Being present in this folder does not by itself guarantee that the
 * stock gallery has indexed the file.
 */
export const GALLERY_DIRECTORY = '/userdisk/Pictures'
export const APP_ALBUM_DIRECTORY = '/userdisk/SnapPhoto'
export function safeAbsolutePath(value) {
  return typeof value === 'string' &&
    value.startsWith('/') &&
    value.length <= 1024 &&
    !value.includes('\\') &&
    !value.includes('\0') &&
    value !== '/' &&
    !value.split('/').some(s => s === '..' || s === '.')
}
export function withinDirectory(path, directory) {
  return safeAbsolutePath(path) && safeAbsolutePath(directory) &&
    path !== directory &&
    path.startsWith(directory.replace(/\/+$/, '') + '/')
}
export function galleryResult(capture) {
  if (!capture || !safeAbsolutePath(capture.path)) throw new Error('摄像头未返回合法的文件绝对路径')
  const inPictures = withinDirectory(capture.path, GALLERY_DIRECTORY)
  return {
    ...capture,
    album: inPictures ? 'pictures' : 'other',
    indexed: inPictures && capture.indexed === true,
    galleryWarning: !inPictures
      ? '文件已拍摄，但尚未保存到 /userdisk/Pictures'
      : (capture.indexed === true ? '' : '文件已保存到 /userdisk/Pictures，原生相册索引状态待确认')
  }
}
export function validateGalleryPublication(publication) {
  if (!publication || publication.written !== true ||
      !withinDirectory(publication.path, GALLERY_DIRECTORY)) {
    throw new Error('写入有道相册目录失败或路径校验未通过')
  }
  return galleryResult(publication)
}
