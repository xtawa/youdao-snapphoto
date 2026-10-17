import qrcode from 'qrcode-generator'
import { qrMatrixToPng } from './qr-png.js'
export function makeQrImage(url) {
  const qr = qrcode(0, 'M')
  qr.addData(url)
  qr.make()
  const count = qr.getModuleCount()
  const matrix = []
  for (let y=0;y<count;y++) {
    const row = []
    for (let x=0;x<count;x++) row.push(qr.isDark(y,x))
    matrix.push(row)
  }
  return qrMatrixToPng(matrix,3,4)
}
