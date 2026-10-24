// Same PhotoAlbum request protocol as the firmware's own album.
export function createStockAlbum(manager){
  let serial=Promise.resolve()
  function request(endpoint,args={}){
    const work=()=>new Promise((resolve,reject)=>{
      let task,timer
      const done=(status,id,data)=>{
        if(id!==task)return
        if(status!==1 && status!==2 && status!==4)return
        clearTimeout(timer);manager.onRequestDataResult.off(done)
        if(status===1){try{resolve(typeof data==='string'?JSON.parse(data):data)}catch(e){reject(e)}}
        else reject(new Error('系统相册请求失败：'+status))
      }
      manager.onRequestDataResult.on(done)
      try{task=manager.requestData('local/interface/'+endpoint,args)}
      catch(e){manager.onRequestDataResult.off(done);reject(e);return}
      timer=setTimeout(()=>{manager.onRequestDataResult.off(done);reject(new Error('系统相册响应超时'))},6000)
    })
    const result=serial.then(work,work);serial=result.catch(()=>{});return result
  }
  return {request,scan:()=>request('scanImages')}
}

export function stockImageRows(payload){
  const result=typeof payload.result==='string'?JSON.parse(payload.result):payload.result
  return result&&Array.isArray(result.data)?result.data.map(row=>Array.isArray(row)?{item_id:row[1],name:row[2],local_path:row[3],thumbnail_path:row[4]}:row):[]
}
