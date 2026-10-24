/**
 * HDR/AI capture capabilities are reported by the NATIVE device bridge.
 * A three-exposure bracket must be captured and aligned natively; changing
 * picture brightness three times is not a genuine HDR capture.
 */
export const HDR_MODES = Object.freeze(['off','auto','on'])
export const DENOISE_MODES = Object.freeze(['off','auto','quality'])
export const HDR_EXPOSURES_EV = Object.freeze([-2,0,2])

export function normalizeProcessing(value) {
  const p=value && typeof value==='object'?value:{}
  return {
    hdrMode:HDR_MODES.includes(p.hdrMode)?p.hdrMode:'off',
    denoiseMode:DENOISE_MODES.includes(p.denoiseMode)?p.denoiseMode:'off'
  }
}
export function normalizeProcessingCapabilities(caps) {
  const c=caps && typeof caps==='object'?caps:{}
  const hdrStrategies=Array.isArray(c.hdrStrategies)?c.hdrStrategies.filter(x=>['isp','bracket'].includes(x)):[]
  return {
    hdr:c.hdr===true && hdrStrategies.length>0,
    hdrStrategies,
    enhancement:c.enhancement===true,
    npu: c.npu===true,
    npuReady:c.npuReady===true && c.npu===true,
    npuBackend:c.npu===true && typeof c.npuBackend==='string'?c.npuBackend:'none'
  }
}
export function chooseCaptureProcessing(prefs,caps) {
  const p=normalizeProcessing(prefs)
  const c=normalizeProcessingCapabilities(caps)
  if(p.hdrMode==='on'&&!c.hdr)throw new Error('此设备尚未开放 HDR 曝光/合成接口')
  if(p.denoiseMode==='quality'&&!c.enhancement)throw new Error('此设备尚未开放图像增强接口')
  return {
    hdr: c.hdr ? p.hdrMode : 'off',
    denoise:c.enhancement?p.denoiseMode:'off',
    preferNpu:c.enhancement && c.npuReady && p.denoiseMode!=='off',
    exposuresEV:c.hdr && p.hdrMode!=='off' && c.hdrStrategies.includes('bracket')?HDR_EXPOSURES_EV.slice():[],
    // Constrain memory usage for small dictionary-pen environments.
    processingProfile:'balanced'
  }
}
export function processingLabel(result) {
  if(!result||typeof result!=='object')return '处理方式未报告'
  let label='普通拍照'
  if(result.hdrApplied===true)label='HDR '+(result.hdrBackend==='isp'?'ISP':'多帧')
  if(result.npuUsed===true)label+=' · NPU 增强'
  else if(result.denoiseApplied===true)label+=' · CPU/ISP 增强'
  return label
}
