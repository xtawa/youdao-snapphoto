export function duration(seconds) {
  const n = Math.max(0, Math.floor(Number(seconds) || 0))
  return String(Math.floor(n / 60)).padStart(2, '0') + ':' + String(n % 60).padStart(2, '0')
}
export function formatBytes(bytes) {
  const size = Math.max(0, Number(bytes) || 0)
  if (size < 1024) return Math.round(size) + ' B'
  if (size < 1048576) return (size / 1024).toFixed(1) + ' KB'
  return (size / 1048576).toFixed(1) + ' MB'
}
export const FILTERS = [
  { id: 'none', label: '原图' },
  { id: 'mono', label: '黑白' },
  { id: 'sepia', label: '棕褐' },
  { id: 'warm', label: '暖调' },
  { id: 'cool', label: '冷调' },
  { id: 'vintage', label: '复古' },
  { id: 'vivid', label: '鲜艳' },
  { id: 'negative', label: '反色' }
]
export const PHOTO_PROFILES = [
  { id: 'wide', label: '3.33:1', width: 3264, height: 979 },
  { id: 'cinema', label: '2.39:1', width: 3264, height: 1362 },
  { id: 'two-one', label: '2:1', width: 3264, height: 1632 },
  { id: 'sixteen-nine', label: '16:9', width: 3264, height: 1836 },
  { id: 'four-three', label: '4:3', width: 3264, height: 2448 },
  { id: 'square', label: '1:1', width: 2448, height: 2448 }
]

export const VIDEO_PROFILES = [
  {id:'compact',label:'省空间',width:640,height:192,portraitWidth:216,portraitHeight:720,targetFps:30},
  {id:'hd',label:'720p',width:1280,height:720,portraitWidth:720,portraitHeight:1280,targetFps:30},
  {id:'fhd',label:'1080p',width:1920,height:1080,portraitWidth:1080,portraitHeight:1920,targetFps:24}
]
