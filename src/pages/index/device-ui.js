/* X7 Pro: fixed native coordinates, large touch targets and camera-first UI. */
import {galleryWindow} from '../../utils/gallery-window.js'
function rect(x,y,w,h,extra){return {position:'absolute',left:x+'px',top:y+'px',width:w+'px',height:h+'px',...extra}}
export function renderDeviceUI(h){
  const vm=this,p=this.orientation==='portrait',w=p?280:936,ht=p?936:280
  const nodes=[],camera=this.panel==='camera',mode=this.mode==='photo'?'拍照':'录像'
  const label=(s,x,y,bw,bh,size=26,color='#ffffff',extra={})=>h('text',{style:rect(x,y,bw,bh,{fontSize:size+'px',lineHeight:bh+'px',color,lines:1,...extra})},String(s))
  const button=(s,x,y,bw,bh,fn,active=false,size=24)=>h('div',{style:rect(x,y,bw,bh,{backgroundColor:active?'#ffffff':'rgba(28,30,33,0.86)',borderRadius:'14px',justifyContent:'center',alignItems:'center'}),on:{click:()=>{if(Date.now()<(vm._ignoreClickUntil||0))return;fn()}}},[h('text',{style:{fontSize:size+'px',color:active?'#17191c':'#ffffff',textAlign:'center',lines:1}},s)])
  const image=(src,x,y,bw,bh,resize='contain',key)=>h('img',{key,attrs:{src,resize},style:rect(x,y,bw,bh)})
  const title=s=>{nodes.push(button('‹',16,16,52,52,()=>{if(vm.panel==='media')vm.leaveMedia();else vm.returnCamera()},false,44),label(s,88,16,w-180,52,28))}
  if(camera){
    const view=this.previewBounds||{x:0,y:0,width:w,height:ht}
    if(this.preview)nodes.push(image(this.preview,view.x,view.y,view.width,view.height,'contain','preview-'+this.sessionId+'-'+this.orientation+'-'+(this.rotation||0)))
    else nodes.push(label(this.connected?'正在打开取景…':'相机未连接',p?15:260,ht/2-26,p?250:430,52,26,'#cccccc',{textAlign:'center'}))
    if(this.selfieActive){
      if(this.selfieHint)nodes.push(h('div',{style:rect(p?12:180,ht/2-42,p?256:576,84,{backgroundColor:'rgba(0,0,0,.7)',justifyContent:'center',alignItems:'center'})},[h('text',{style:{fontSize:'26px',color:'#ffffff',textAlign:'center',lines:2}},'双击任意位置拍照 · 下滑退出')]))
      if(this.countdown)nodes.push(label(this.countdown,p?85:408,ht/2-64,110,128,84,'#ffffff',{textAlign:'center'}))
      if(this.selfieFlash)nodes.push(h('div',{style:rect(0,0,w,ht,{backgroundColor:'rgba(255,255,255,.65)'})}))
      nodes.push(h('div',{style:rect(0,0,w,ht),on:{touchstart:this.gestureStart,touchend:this.gestureEnd,touchcancel:this.gestureCancel}}))
      return h('div',{style:{...this.screenStyle,position:'absolute',backgroundColor:'#000000'}},nodes)
    }
    nodes.push(h('div',{style:rect(0,0,w,ht),on:{touchstart:this.gestureStart,touchend:this.gestureEnd,touchcancel:this.gestureCancel}}))
    if(this.showGrid){for(const k of [1,2])nodes.push(h('div',{style:rect(view.x+view.width*k/3,view.y,1,view.height,{backgroundColor:'rgba(255,255,255,.28)'})}),h('div',{style:rect(view.x,view.y+view.height*k/3,view.width,1,{backgroundColor:'rgba(255,255,255,.28)'})}))}
    nodes.push(button('×',16,16,52,52,()=>this.exit(),false,42),button('设置',w-68,16,52,52,()=>{this.panel='settings'},false,18))
    const sx=p?w/2-38:w-98,sy=p?ht-112:ht/2-38
    nodes.push(h('div',{style:rect(sx,sy,76,76,{borderRadius:'38px',borderWidth:'4px',borderColor:'#ffffff',justifyContent:'center',alignItems:'center',backgroundColor:'rgba(0,0,0,.3)'}),on:{click:()=>{if(Date.now()>=(this._ignoreClickUntil||0))this.shutter()}}},[h('div',{style:{width:(this.recording?'32':'58')+'px',height:(this.recording?'32':'58')+'px',borderRadius:(this.recording?'7':'29')+'px',backgroundColor:this.mode==='video'?'#f35961':'#ffffff'}})]))
    nodes.push(button('相册',w-68,ht-68,52,52,()=>this.gallery(),false,18))
    nodes.push(button('自拍',16,p?92:ht-68,52,52,()=>this.enterSelfie('manual'),false,18))
    nodes.push(button(this.zoom.toFixed(1)+'×',p?16:w-200,ht-68,76,48,()=>this.zoomBy(this.zoom>=3?1-this.zoom:.5),false,23))
    const choices=['拍照','录像'],start=p?32:332,by=p?ht-184:ht-60
    choices.forEach((s,i)=>nodes.push(button(s,start+i*112,by,100,44,()=>this.changeMode(i?'video':'photo'),mode===s,24)))
    if(this.mode==='video'&&!this.recording)nodes.push(label(this.videoProfileLabel||'录像',p?88:92,20,150,36,22,'#c7e8da'))
    if(this.busy&&this.mode==='photo'&&!this.countdown)nodes.push(label('正在保存…',p?76:290,16,p?132:360,42,22,'#ffffff',{textAlign:'center'}))
    else if(this.processingPending&&!this.recording)nodes.push(label('后台增强 '+this.processingPending+' 张 · '+this.processingProgress+'%',p?16:300,p?118:16,p?248:390,36,20,'#c7e8da'))
    if(this.nightActive&&!this.recording)nodes.push(label('暗光增强',p?88:120,p?68:58,130,28,18,'#f5dca0'))
    if(this.recording)nodes.push(label('● '+this.elapsed+(this.caps&&this.caps.audio?' · 录音':''),p?72:350,16,p?145:260,44,p?20:26,'#ff6972'))
    else if(this.timerDelay||this.burstCount>1)nodes.push(label((this.timerDelay?this.timerDelay+'秒 ':'')+(this.burstCount>1?this.burstCount+'张':''),p?85:92,20,180,36,22))
    if(this.countdown)nodes.push(label(this.countdown,p?85:408,ht/2-64,110,128,84,'#ffffff',{textAlign:'center'}))
  }else if(this.panel==='settings'){
    title('拍摄设置')
    nodes.push(label('预览更新 '+this.previewUpdates+' 帧/秒'+(this.caps&&this.caps.continuousFocus?' · 系统连续对焦':''),p?16:94,p?72:62,p?248:720,26,p?15:18,'#9fa9b3'))
    const pages=[[
      ['倒计时',this.timerDelay?this.timerDelay+' 秒':'关闭',()=>this.setTimer(this.timerChoices[(this.timerChoices.indexOf(this.timerDelay)+1)%this.timerChoices.length])],
      ['连续拍摄',this.burstCount+' 张',()=>this.setBurst(this.burstChoices[(this.burstChoices.indexOf(this.burstCount)+1)%this.burstChoices.length])],
      ['辅助线',this.showGrid?'开启':'关闭',()=>this.toggleGrid()]
    ],[
      ['照片比例',p?'仅横屏可调':this.photoProfileLabel,()=>{if(p)this.message('照片比例仅在横屏下可调');else this.panel='ratios'}],
      ['画面方向',this.layoutLabel,()=>this.cycleLayout()],
      ['滤镜',this.filters.find(f=>f.id===this.filter).label,()=>this.chooseFilter(this.filters[(this.filters.findIndex(f=>f.id===this.filter)+1)%this.filters.length].id)]
    ],[
      ['局域网导出','选择照片',()=>this.gallery()],
      ['翻转自拍',this.autoSelfie?'开启':'关闭',()=>this.toggleAutoSelfie()],
      ['全屏自拍','立即进入',()=>this.enterSelfie('manual')]
    ],[
      ['录像画质',this.videoProfileLabel,()=>{this.panel='videoQuality'}],
      ['目标帧率',this.videoProfile==='fhd'?'24 帧/秒':'30 帧/秒',()=>this.message('实际帧率受场景、滤镜和设备负载影响')],
      ['录像格式','AVI · 有声',()=>this.message('高清 MJPEG 文件较大，请留意剩余空间')]
    ],[
      ['暗光模式',this.nightMode==='auto'?'自动':'关闭',()=>this.cycleNight()],
      ['暗光取舍',this.nightPriority==='quality'?'质量优先':'速度优先',()=>this.cycleNightPriority()],
      ['增强状态',this.nightActive?'正在提亮':'正常光线',()=>this.message('自动检测亮度，保护高光并平滑切换')]
    ],[
      ['HDR 拍照',this.hdrAvailable?this.hdrLabel:'未开放',()=>this.cycleHDR()],
      ['拍后降噪',this.enhanceAvailable?this.denoiseLabel:'未就绪',()=>this.cycleDenoise()],
      ['NPU 增强',this.npuReady?'已就绪':'未就绪',()=>this.message(this.npuReady?'是否使用 NPU 以拍照结果为准':'暂无已验证的增强模型，保留现有 CPU 暗光处理')]
    ],[
      ['后台增强','拍完自动处理',()=>this.message('原片先保存，增强排队生成另一张照片')],
      ['后台保活',this.processingKeepAlive?'退出后继续':'退出后暂停',()=>this.toggleProcessingKeepAlive()],
      ['处理队列',this.processingPending?this.processingPending+' 张 · '+this.processingProgress+'%':'空闲',()=>this.pollProcessing()]
    ]]
    const page=this.settingsPage%pages.length
    pages[page].forEach((item,i)=>{const x=p?16:94+i*268,y=p?108+i*180:90,bw=p?248:248,bh=p?150:114;nodes.push(h('div',{style:rect(x,y,bw,bh,{backgroundColor:'#24272b',borderRadius:'16px'}),on:{click:item[2]}},[label(item[0],18,14,bw-36,38,24,'#afb4ba'),label(item[1],18,p?64:54,bw-36,44,item[0]==='照片比例'?20:30)]))})
    nodes.push(button('‹',p?24:332,ht-60,72,44,()=>{this.settingsPage=(page+pages.length-1)%pages.length}),label((page+1)+' / '+pages.length,p?120:435,ht-60,70,44,22,'#aaaaaa'),button('›',p?184:532,ht-60,72,44,()=>{this.settingsPage=(page+1)%pages.length}))
  }else if(this.panel==='videoQuality'){
    title('录像画质')
    this.videoProfileList.forEach((profile,i)=>{
      const x=p?16:94+i*268,y=p?108+i*180:90,bw=248,bh=p?150:114,active=this.videoProfile===profile.id
      const rw=p?profile.portraitWidth:profile.width,rh=p?profile.portraitHeight:profile.height
      nodes.push(h('div',{style:rect(x,y,bw,bh,{backgroundColor:active?'#c7e8da':'#24272b',borderRadius:'16px'}),on:{click:()=>this.applyVideoProfile(profile.id)}},[
        label(profile.label,18,10,bw-36,38,30,active?'#18372a':'#ffffff'),
        label(rw+'×'+rh+' · '+profile.targetFps+'帧',18,p?66:56,bw-36,38,20,active?'#345f4b':'#afb4ba')
      ]))
    })
    nodes.push(label('目标帧率 · 高清文件较大',p?16:94,p?700:222,p?248:700,40,p?18:22,'#afb4ba'))
  }else if(this.panel==='ratios'){
    title('横屏照片比例')
    this.photoProfileList.forEach((profile,i)=>{
      const x=94+(i%3)*268,y=82+Math.floor(i/3)*80,active=this.photoProfile===profile.id
      nodes.push(h('div',{style:rect(x,y,248,70,{backgroundColor:active?'#c7e8da':'#24272b',borderRadius:'12px'}),on:{click:()=>this.applyPhotoProfile(profile.id)}},[
        label(profile.label.split(' ')[0],16,4,216,32,25,active?'#18372a':'#ffffff'),
        label(profile.width+' × '+profile.height,16,36,216,28,19,active?'#345f4b':'#afb4ba')
      ]))
    })
    nodes.push(label('切换后按完整照片比例取景',94,246,650,26,19,'#aaaaaa'))
  }else if(this.panel==='gallery'){
    title('相册 · '+this.media.length)
    nodes.push(h('div',{style:rect(w-100,16,84,52),on:{click:()=>{this.selecting=!this.selecting;if(!this.selecting)this.clearSelection()}}},[h('text',{style:{fontSize:'28px',color:'#00b5ab',lineHeight:'52px',textAlign:'center'}},this.selecting?'完成':'选择')]))
    const window=galleryWindow(this.visibleMedia.length,this.orientation,this.galleryRow,this.selecting)
    const {columns:cols,size,pitch,height:viewHeight}=window,left=p?14:80
    const tiles=[]
    // Stable tile containers preserve native scroll extent/position. Only the
    // images in the viewport plus overscan are mounted, with no 20-tile cap.
    this.visibleMedia.forEach((file,at)=>{
      const x=(at%cols)*pitch,y=Math.floor(at/cols)*pitch,selected=this.selected.indexOf(file.path)>=0
      const visible=at>=window.start&&at<window.end
      const children=visible&&file.thumbnail?[image('file://'+file.thumbnail,0,0,size,size,'cover',file.path+'-'+file.thumbnail)]:[]
      if(visible&&!file.thumbnail)children.push(label('…',0,0,size,size,22,'#999999',{textAlign:'center'}))
      if(visible&&file.type==='video')children.push(label('▶',8,size-40,40,32,22))
      if(visible&&this.selecting)children.push(label(selected?'●':'○',size-38,4,32,32,27,selected?'#00b5ab':'#ffffff'))
      tiles.push(h('div',{key:file.path,style:rect(x,y,size,size,{backgroundColor:'#24272b'}),on:{click:()=>this.openMedia(file)}},children))
    })
    nodes.push(h('scroller',{key:'album-'+this.orientation+'-'+this.mediaTypeFilter,style:rect(left,84,cols*pitch-4,viewHeight),attrs:{scrollDirection:'vertical',showScrollbar:false,scrollEventInterval:80},on:{scroll:this.galleryScroll,scrollend:this.galleryScroll}},[
      h('div',{style:{width:(cols*pitch-4)+'px',height:window.totalHeight+'px',position:'relative'}},tiles)
    ]))
    if(!this.media.length)nodes.push(label('还没有照片或视频',p?16:330,120,p?248:340,50,24))
    if(this.selecting){
      nodes.push(button(this.visibleMedia.length&&this.visibleMedia.every(f=>this.selected.includes(f.path))?'取消全选':'全选',p?16:320,p?ht-118:ht-58,p?112:84,42,()=>this.selectAllVisible(),false,20),button('删除',p?148:420,p?ht-118:ht-58,p?116:84,42,()=>this.requestDelete(),false,20),button('导出 '+this.selected.length,p?148:720,p?ht-58:ht-58,p?116:164,42,()=>this.sharePanel(),true,20))
    }
    if(this.selecting)nodes.push(button('入系统相册',p?16:520,p?ht-58:ht-58,p?112:184,42,()=>this.importSelected(),false,20))
    if(this.deleteConfirm){
      nodes.push(h('div',{style:rect(0,0,w,ht,{backgroundColor:'rgba(0,0,0,.92)'})},[
        label('删除选中的 '+this.selected.length+' 个文件？',p?16:180,p?280:60,p?248:650,58,24),
        label('原文件将被删除，无法撤销',p?16:180,p?344:118,p?248:650,44,20,'#e4a5a5'),
        button('取消',p?16:210,p?416:186,p?112:220,52,()=>{this.deleteConfirm=false}),button('确认删除',p?148:470,p?416:186,p?116:220,52,()=>this.deleteSelected(),true,23)
      ]))
    }
  }else if(this.panel==='media'){
    title(this.mediaItem&&this.mediaItem.type==='video'?'录像预览':'照片预览')
    // Keep the native image view alive between video frames. Replacing its key
    // for every JPEG destroys the old view before the next decode completes.
    if(this.mediaPreview)nodes.unshift(image(this.mediaPreview,0,0,w,ht,'contain','media-'+(this.mediaItem&&this.mediaItem.path)))
    nodes.push(button('‹',16,ht/2-22,52,44,()=>this.mediaChange(-1),false,30),button('›',w-68,ht/2-22,52,44,()=>this.mediaChange(1),false,30))
    if(this.mediaItem&&this.mediaItem.type==='video')nodes.push(button(this.playing?'停止':'▶ 播放',p?48:350,ht-58,p?184:236,44,()=>this.playMedia(),true,22))
  }else if(this.panel==='share'){
    title('局域网导出')
    const chosen=this.chosenFiles.map(path=>this.media.find(f=>f.path===path)).filter(f=>f)
    const totalBytes=chosen.reduce((sum,f)=>sum+(Number(f.size)||0),0)
    if(this.qr)nodes.push(image(this.qr,p?30:684,p?84:76,p?220:176,p?220:176))
    else nodes.push(label(this.shareUrl?'请用下方网址下载':'开启分享后显示二维码',p?30:666,p?150:130,p?220:250,64,18,'#aaaaaa',{textAlign:'center',lines:2,lineHeight:'28px'}))
    nodes.push(label(chosen.length?('已选 '+chosen.length+' 项 · '+this.size(totalBytes)):'请先到相册选择照片',p?16:96,p?322:78,p?248:550,34,24))
    nodes.push(button(this.shareStarting?'开启中…':(this.shareUrl?'结束分享':'开启分享'),p?16:96,p?378:126,p?248:224,52,()=>{if(!this.shareStarting)this.shareUrl?this.stopShare():this.share()},true,26))
    if(this.shareUrl){
      nodes.push(label('剩余 '+this.shareRemaining,p?16:350,p?440:132,p?248:280,36,22,'#ffd28a'))
      const parts=this.shareUrl.split('/'),address=parts.slice(0,3).join('/')+'/\n'+parts.slice(3).join('/')
      nodes.push(label(address,p?16:96,p?530:224,p?248:550,p?100:48,16,'#b1e1c8',{lines:p?4:2,lineHeight:'23px'}))
    }
    nodes.push(label('手机与词典笔连接同一 Wi-Fi',p?16:96,p?486:190,p?248:550,28,19,'#aaaaaa'))
    if(p)nodes.push(label('扫码下载原文件，保留拍摄时间',16,650,248,60,19,'#8a9098',{lines:2,lineHeight:'28px'}))

  }
  if(this.toast)nodes.push(h('div',{style:rect(p?12:196,ht-(p?220:72),p?256:544,p?100:56,{backgroundColor:'rgba(38,42,47,.96)',borderRadius:'12px',justifyContent:'center',paddingLeft:'14px',paddingRight:'14px'})},[h('text',{style:{fontSize:'22px',color:'#ffffff',lines:2}},this.toast)]))
  const st={...this.screenStyle,position:'absolute',backgroundColor:'#000000'}
  return h('div',{style:st,on:camera?{}:{touchstart:this.gestureStart,touchend:this.gestureEnd,touchcancel:this.gestureCancel}},nodes)
}
