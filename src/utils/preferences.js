const KEY='snapphoto.preferences.v1'
export const DEFAULTS=Object.freeze({timerDelay:0,burstCount:1,showGrid:false,layoutMode:'auto'})
export function normalizePreferences(value){
  const p=value && typeof value==='object'?value:{}
  return {
    timerDelay:[0,3,5,10].indexOf(Number(p.timerDelay))>=0?Number(p.timerDelay):0,
    burstCount:[1,3,5].indexOf(Number(p.burstCount))>=0?Number(p.burstCount):1,
    showGrid:p.showGrid===true,
    layoutMode:['auto','portrait','landscape'].indexOf(p.layoutMode)>=0?p.layoutMode:'auto'
  }
}
function storage(scope){
  const s=scope || (typeof globalThis!=='undefined'?globalThis:{})
  return s.$falcon && s.$falcon.jsapi && s.$falcon.jsapi.storage
}
export async function loadPreferences(scope){
  const api=storage(scope)
  if(!api||typeof api.getStorage!=='function')return {...DEFAULTS}
  try{
    const obj=await api.getStorage({key:KEY})
    const raw=obj && obj.data
    return normalizePreferences(typeof raw==='string'?JSON.parse(raw):raw)
  }catch(e){return {...DEFAULTS}}
}
export async function savePreferences(p,scope){
  const api=storage(scope)
  if(!api||typeof api.setStorage!=='function')return false
  try{await api.setStorage({key:KEY,data:JSON.stringify(normalizePreferences(p))});return true}
  catch(e){return false}
}
