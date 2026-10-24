// Native scrollers emit y; wrappers may forward the payload through detail.
export function scrollOffset(event){
  let value=event
  if(typeof value==='string'){try{value=JSON.parse(value)}catch(e){return null}}
  if(!value||typeof value!=='object')return null
  const detail=value.detail
  const candidates=[value.y,value.contentOffset&&value.contentOffset.y,value.contentYOffset,
    value.scrollTop,detail&&detail.y,detail&&detail.contentOffset&&detail.contentOffset.y,detail&&detail.scrollTop]
  for(const candidate of candidates){
    if(candidate===null||candidate===undefined||candidate==='')continue
    const number=typeof candidate==='number'?candidate:parseFloat(candidate)
    if(Number.isFinite(number))return Math.abs(number)
  }
  return null
}
export function galleryWindow(count,orientation,row,selecting=false){
  const portrait=orientation==='portrait',columns=portrait?2:5,size=portrait?124:160,pitch=size+4
  const height=(portrait?936:280)-(selecting?(portrait?208:144):84)
  const rows=Math.ceil(Math.max(0,count)/columns),visibleRows=Math.ceil(height/pitch)+1
  const first=Math.max(0,Math.min(Math.max(0,rows-1),Math.floor(row)||0))
  // Keep one row behind and ahead so a slow decoder never leaves a swipe blank.
  return {columns,size,pitch,height,row:first,start:Math.max(0,first-1)*columns,
    end:Math.min(count,(first+visibleRows+1)*columns),totalHeight:Math.max(height,rows*pitch)}
}
