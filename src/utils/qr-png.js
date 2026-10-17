/**
 * Small dependency-free PNG encoder for a QR-code boolean matrix.
 * qrcode-generator builds the matrix, and this renders it to PNG.
 * Works without Buffer, Canvas, DOM, or browser APIs (QuickJS).
 */
function u32(n) {
  return [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255]
}
function crc32(data) {
  let crc = 0xffffffff
  for (let i = 0; i < data.length; i++) {
    crc ^= data[i]
    for (let j = 0; j < 8; j++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0)
  }
  return (crc ^ 0xffffffff) >>> 0
}
function adler32(data) {
  let a = 1, b = 0
  for (let i = 0; i < data.length; i++) {
    a = (a + data[i]) % 65521
    b = (b + a) % 65521
  }
  return ((b << 16) | a) >>> 0
}
function chunk(type, bytes) {
  const name = type.split('').map(c => c.charCodeAt(0))
  return [...u32(bytes.length), ...name, ...bytes, ...u32(crc32([...name, ...bytes]))]
}
function zlibStore(data) {
  const output = [0x78, 0x01]
  for (let start = 0; start < data.length; start += 65535) {
    const end = Math.min(data.length, start + 65535)
    const len = end - start
    output.push(end === data.length ? 1 : 0, len & 255, len >>> 8, (~len) & 255, ((~len) >>> 8) & 255)
    for (let i = start; i < end; i++) output.push(data[i])
  }
  output.push(...u32(adler32(data)))
  return output
}
function base64(bytes) {
  const letters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
  let out = ''
  for (let i = 0; i < bytes.length; i += 3) {
    const a = bytes[i], b = bytes[i + 1], c = bytes[i + 2]
    const word = (a << 16) | ((b || 0) << 8) | (c || 0)
    out += letters[(word >>> 18) & 63] + letters[(word >>> 12) & 63] +
      (i + 1 < bytes.length ? letters[(word >>> 6) & 63] : '=') +
      (i + 2 < bytes.length ? letters[word & 63] : '=')
  }
  return out
}
export function qrMatrixToPng(matrix, scale = 3, quiet = 4) {
  const n = matrix.length
  if (!n || matrix.some(row => row.length !== n)) throw new Error('无效二维码矩阵')
  const side = (n + quiet * 2) * scale
  if (side > 256 || side < 1) throw new Error('二维码图片尺寸超出限制')
  const pixels = []
  for (let y = 0; y < side; y++) {
    pixels.push(0)
    for (let x = 0; x < side; x++) {
      const col = Math.floor(x / scale) - quiet
      const row = Math.floor(y / scale) - quiet
      pixels.push(row >= 0 && row < n && col >= 0 && col < n && matrix[row][col] ? 0 : 255)
    }
  }
  const header = [...u32(side), ...u32(side), 8, 0, 0, 0, 0]
  const bytes = [
    137, 80, 78, 71, 13, 10, 26, 10,
    ...chunk('IHDR', header),
    ...chunk('IDAT', zlibStore(pixels)),
    ...chunk('IEND', [])
  ]
  return 'data:image/png;base64,' + base64(bytes)
}
