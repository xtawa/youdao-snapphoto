export function detectOrientation(scope) {
  const s=scope || (typeof globalThis!=='undefined'?globalThis:{})
  const pairs=[[s.innerWidth,s.innerHeight],[s.screen && s.screen.width,s.screen && s.screen.height]]
  for(const pair of pairs){
    const w=Number(pair[0]),h=Number(pair[1])
    if(w>0 && h>0 && Number.isFinite(w) && Number.isFinite(h))return h>w?'portrait':'landscape'
  }
  return 'landscape'
}
export function effectiveOrientation(preference,detected){
  if(preference==='portrait'||preference==='landscape')return preference
  return detected==='portrait'?'portrait':'landscape'
}
