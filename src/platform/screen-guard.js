// Verified no-argument methods on this firmware's global.Global proxy.
// This is a foreground request; it never edits the user's screen timeout.
export function createScreenGuard(api){
  let active=false
  return {
    start(){
      if(active)return true
      if(typeof api.startAlwaysScreenOn!=='function'||typeof api.stopAlwaysScreenOn!=='function')return false
      api.startAlwaysScreenOn();active=true;return true
    },
    stop(){
      if(!active)return
      try{api.stopAlwaysScreenOn()}finally{active=false}
    }
  }
}
