export function touchPoint(event){
  const list=event && (event.changedTouches || event.touches)
  const p=list && list[0]
  return p && Number.isFinite(Number(p.pageX)) && Number.isFinite(Number(p.pageY))
    ? {x:Number(p.pageX),y:Number(p.pageY)} : null
}
export function swipeStep(start,end,portrait=false){
  if(!start||!end)return 0
  const dx=portrait?end.y-start.y:end.x-start.x
  const dy=portrait?end.x-start.x:end.y-start.y
  if(Math.abs(dx)<65||Math.abs(dx)<Math.abs(dy)*1.5)return 0
  return dx<0?1:-1
}
