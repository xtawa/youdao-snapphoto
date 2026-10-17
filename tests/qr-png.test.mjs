import test from 'node:test'
import assert from 'node:assert/strict'
import {inflateSync} from 'node:zlib'
import {qrMatrixToPng} from '../src/utils/qr-png.js'
test('encodes valid PNG from QR matrix', () => {
  const data = qrMatrixToPng([[true,false],[false,true]],2,1).split(',')[1]
  const png = Buffer.from(data, 'base64')
  assert.equal(png.subarray(1,4).toString(),'PNG')
  assert.equal(png.readUInt32BE(16),8)
  const size = png.readUInt32BE(33)
  const compressed = png.subarray(41,41 + size)
  assert.equal(inflateSync(compressed).length,8*9)
})
