// Letterbox the complete crop. The sensor stream is never renegotiated.
export function previewRect(orientation,mode,profiles,id,videoProfiles,videoId){
  const portrait=orientation==='portrait',w=portrait?280:936,h=portrait?936:280
  const p=mode!=='video'&&Array.isArray(profiles)&&profiles.find(p=>p.id===id)
  const video=mode==='video'&&Array.isArray(videoProfiles)&&videoProfiles.find(p=>p.id===videoId)
  const aspect=video?(portrait?video.portraitWidth/video.portraitHeight:video.width/video.height):p?(portrait?p.height/p.width:p.width/p.height):portrait?216/720:640/192
  const height=Math.min(h,w/aspect),width=height*aspect
  return {x:(w-width)/2,y:(h-height)/2,width,height}
}
