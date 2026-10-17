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
  { id: 'warm', label: '暖调' },
  { id: 'cool', label: '冷调' },
  { id: 'vintage', label: '复古' }
]
