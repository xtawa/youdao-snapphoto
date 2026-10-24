// Follow the stock camera lifecycle: subscribe, enable, disable, unsubscribe.
export function createOrientationSensor(module, fallback){
  let sensor=null,enabled=false,latest=null
  const update=input=>{
    if(!enabled)return
    try{
      const value=typeof input==='string'?JSON.parse(input):input
      if(value&&Number.isInteger(value.x)&&Number.isInteger(value.z))latest={x:value.x,y:value.y,z:value.z,available:true}
    }catch(ignored){}
  }
  return {
    start(){
      if(enabled)return true
      const api=module.Gsensor?module:module.default
      if(!api||typeof api.Gsensor!=='function')return false
      if(!sensor)sensor=new api.Gsensor()
      latest=null;enabled=true
      try{sensor.orientResChange.on(update);sensor.wakeupEnable()}
      catch(error){enabled=false;try{sensor.orientResChange.off(update)}catch(ignored){};throw error}
      return true
    },
    stop(){
      if(!enabled)return
      enabled=false;latest=null
      try{sensor.wakeupDisable()}finally{sensor.orientResChange.off(update)}
    },
    read(){
      if(!enabled)return {available:false}
      const value=fallback()
      return value&&value.available===true?value:(latest||{available:false})
    }
  }
}
