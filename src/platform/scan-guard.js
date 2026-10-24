// The stock AI camera registers its appid alone to consume head presses without OCR.
export function createScanGuard(api, appid){
  let active=false
  return {
    start(){
      if(active)return
      if(typeof api.addUseOcrAppId!=='function'||typeof api.deleteUseOcrAppId!=='function')throw new Error('系统扫描拦截接口不可用')
      api.addUseOcrAppId(JSON.stringify({appid}))
      active=true
    },
    stop(){
      if(!active)return
      api.deleteUseOcrAppId(appid)
      active=false
    }
  }
}
