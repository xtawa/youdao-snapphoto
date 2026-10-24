import {OrientationTracker,displayRotation,captureRotation} from '../../utils/auto-orientation.js'
import {galleryWindow,scrollOffset} from '../../utils/gallery-window.js'
import {previewRect} from '../../utils/photo-layout.js'
import {renderDeviceUI} from './device-ui.js'
import {touchPoint,swipeStep} from '../../utils/swipe.js'
import {DoubleTap,FlipTracker} from '../../utils/selfie.js'
import {createCameraBridge} from '../../platform/camera-bridge.js'
import {HDR_MODES,DENOISE_MODES,processingLabel} from '../../platform/processing-policy.js'
import {FILTERS,PHOTO_PROFILES,VIDEO_PROFILES,duration,formatBytes} from '../../utils/format.js'
import {makeQrImage} from '../../utils/qr.js'
import {CaptureSession,TIMER_CHOICES,BURST_CHOICES} from '../../utils/capture-session.js'
import {detectOrientation,effectiveOrientation} from '../../utils/orientation.js'
import {loadPreferences,savePreferences} from '../../utils/preferences.js'

export default {
  name:'index',
  render(h){return renderDeviceUI.call(this,h)},
  data(){return {
    settingsPage:0,galleryPage:0,galleryRow:0,rotation:0,nightMode:'auto',nightPriority:'speed',nightActive:false,mediaItem:null,mediaPreview:'',playing:false,selecting:false,deleteConfirm:false,autoSelfie:true,selfieActive:false,selfieHint:false,selfieFlash:false,sensorAvailable:false,bridge:null,caps:null,connected:false,busy:false,recording:false,seconds:0,recorderTimer:null,
    previewUpdates:0,sourceFps:0,frameTimer:null,orientationTimer:null,preview:'',zoom:1,mode:'photo',filter:'none',panel:'camera',
    media:[],selected:[],shareUrl:'',qr:'',status:'检测 X7 Pro 摄像头…',toast:'',toastTimer:null,
    timerChoices:TIMER_CHOICES,burstChoices:BURST_CHOICES,
    captureSession:new CaptureSession(),countdown:0,shotsCompleted:0,
    timerDelay:0,burstCount:1,showGrid:false,layoutMode:'auto',photoProfile:'wide',videoProfile:'hd',detectedOrientation:'landscape',
    mediaTypeFilter:'all',connecting:false,sessionId:0,lastGalleryWarning:'',shareExpiresAt:0,shareCountdown:0,shareStarting:false,
    hdrMode:'off',denoiseMode:'off',hdrModes:HDR_MODES,denoiseModes:DENOISE_MODES,lastProcessing:'',processingKeepAlive:true,processingPending:0,processingProgress:0
  }},
  computed:{
    screenStyle(){return this.orientation==='portrait'
      ? {width:'280px',height:'936px',left:'328px',top:'-328px',transform:'rotate('+displayRotation(this.layoutMode,this.orientation,this.rotation)+'deg)'}
      : {width:'936px',height:'280px',left:'0px',top:'0px',transform:'rotate('+displayRotation(this.layoutMode,this.orientation,this.rotation)+'deg)'}},
    elapsed(){return duration(this.seconds)},
    filters(){return FILTERS.filter(f=>f.id==='none'||(this.caps&&this.caps.filters.indexOf(f.id)>=0))},
    orientation(){return effectiveOrientation(this.layoutMode,this.detectedOrientation)},
    galleryColumns(){return this.orientation==='portrait'?2:5},
    visibleMedia(){
      if(this.mediaTypeFilter==='all')return this.media
      return this.media.filter(f=>f.type===this.mediaTypeFilter)
    },
    chosenFiles(){return this.selected},
    hdrAvailable(){return Boolean(this.caps&&this.caps.hdr)},
    enhanceAvailable(){return Boolean(this.caps&&this.caps.enhancement)},
    npuReady(){return Boolean(this.caps&&this.caps.npuReady)},
    hdrLabel(){return this.hdrMode==='off'?'关闭':this.hdrMode==='auto'?'自动':'开启'},
    denoiseLabel(){return this.denoiseMode==='off'?'关闭':this.denoiseMode==='auto'?'自动':'画质优先'},
    layoutLabel(){return this.layoutMode==='auto'?'自动':(this.layoutMode==='portrait'?'竖屏':'横屏')},
    photoProfileList(){return this.caps&&this.caps.photoProfiles&&this.caps.photoProfiles.length?this.caps.photoProfiles.map(p=>({id:p.id,width:p.width,height:p.height,label:(PHOTO_PROFILES.find(m=>m.id===p.id)||{label:p.id}).label+' '+p.width+'×'+p.height})):PHOTO_PROFILES.map(p=>({...p}))},
    photoProfileLabel(){const f=this.photoProfileList.find(p=>p.id===this.photoProfile);return f?f.label:'超宽 3.33:1'},
    videoProfileList(){
      const supported=this.caps&&this.caps.videoProfiles
      return supported&&supported.length?supported.map(p=>({...p,label:(VIDEO_PROFILES.find(v=>v.id===p.id)||p).label||p.id})):[VIDEO_PROFILES[0]]
    },
    videoProfileLabel(){const p=this.videoProfileList.find(p=>p.id===this.videoProfile);return p?p.label:'省空间'},
    previewBounds(){return previewRect(this.orientation,this.mode,this.caps&&this.caps.photoProfiles,this.photoProfile,this.videoProfileList,this.videoProfile)},
    shareRemaining(){return this.shareCountdown>0?duration(this.shareCountdown):''}
  },
  methods:{
    gestureStart(event){this._gestureStart=touchPoint(event);if(this.selfieActive)(this._doubleTap||(this._doubleTap=new DoubleTap())).start(event,Date.now())},
    gestureCancel(){this._gestureStart=null;if(this._doubleTap)this._doubleTap.reset()},
    gestureEnd(event){
      const start=this._gestureStart,end=touchPoint(event),portrait=this.orientation==='portrait'
      if(this.selfieActive){
        this._gestureStart=null
        const down=start&&end&&(portrait?start.x-end.x:end.y-start.y)>90
        if(down){this.leaveSelfie();this.gestureCancel();return}
        if(this._doubleTap&&this._doubleTap.end(event,Date.now())&&!this.busy&&this.connected&&this._visible){
          this.selfieFlash=true;this._flashTimer=setTimeout(()=>{this.selfieFlash=false},120)
          this.shutter()
        }
        return
      }
      const step=swipeStep(start,end,portrait)
      this._gestureStart=null
      if(step&&this.panel==='media'){this.mediaChange(step);return}
      if(!step||this.busy||this.recording||['camera','gallery'].indexOf(this.panel)<0)return
      const current=this.panel==='gallery'?2:(this.mode==='video'?1:0)
      const next=Math.max(0,Math.min(2,current+step))
      if(next===current)return
      this._ignoreClickUntil=Date.now()+350
      if(next===2)this.gallery()
      else this.changeMode(next?'video':'photo')
    },
    message(v){
      this.toast=String(v)
      if(this.toastTimer)clearTimeout(this.toastTimer)
      this.toastTimer=setTimeout(()=>{this.toast=''},3000)
    },
    updateOrientation(){
      if(this.layoutMode==='auto'&&this.sensorAvailable)return
      this.detectedOrientation=detectOrientation()
    },
    async restoreSettings(){
      const p=await loadPreferences()
      this.timerDelay=p.timerDelay;this.burstCount=p.burstCount
      this.showGrid=p.showGrid;this.layoutMode=p.layoutMode;this.autoSelfie=p.autoSelfie;this.photoProfile=p.photoProfile;this.videoProfile=p.videoProfile
      this.nightMode=p.nightMode;this.nightPriority=p.nightPriority
      this.hdrMode=p.hdrMode;this.denoiseMode=p.denoiseMode
      this.processingKeepAlive=p.processingKeepAlive
      this.updateOrientation()
    },
    persist(){
      savePreferences({timerDelay:this.timerDelay,burstCount:this.burstCount,
        showGrid:this.showGrid,layoutMode:this.layoutMode,autoSelfie:this.autoSelfie,photoProfile:this.photoProfile,videoProfile:this.videoProfile,nightMode:this.nightMode,nightPriority:this.nightPriority,hdrMode:this.hdrMode,denoiseMode:this.denoiseMode,processingKeepAlive:this.processingKeepAlive}).catch(()=>{})
    },
    setHDR(mode){
      if(this.busy||this.recording)return this.message('拍摄过程中不可切换 HDR')
      if(!HDR_MODES.includes(mode))return this.message('未知的 HDR 模式')
      if(mode!=='off'&&!this.hdrAvailable)return this.message('此设备尚未接入 HDR 多曝光拍摄')
      this.hdrMode=mode;this.persist()
    },
    cycleHDR(){this.setHDR(HDR_MODES[(HDR_MODES.indexOf(this.hdrMode)+1)%HDR_MODES.length])},
    setDenoise(mode){
      if(this.busy||this.recording)return this.message('拍摄过程中不可切换增强')
      if(!DENOISE_MODES.includes(mode))return this.message('未知的增强模式')
      if(mode!=='off'&&!this.enhanceAvailable)return this.message('拍后增强尚未就绪，暗光增强仍可正常使用')
      this.denoiseMode=mode;this.persist()
    },
    cycleDenoise(){this.setDenoise(DENOISE_MODES[(DENOISE_MODES.indexOf(this.denoiseMode)+1)%DENOISE_MODES.length])},
    toggleProcessingKeepAlive(){this.processingKeepAlive=!this.processingKeepAlive;this.persist();this.message(this.processingKeepAlive?'已排队任务可在退出后继续':'新任务将在退出后暂停，下次打开继续')},
    async pollProcessing(){
      const bridge=this.bridge,id=this.sessionId
      if(!this._visible||!bridge||!bridge.caps||!bridge.caps.backgroundEnhancement||this._processingPolling)return
      this._processingPolling=true
      try{
        const status=await bridge.native.getProcessingStatus()
        if(id!==this.sessionId)return
        this.processingPending=status.pending+status.running
        const running=status.jobs.find(job=>job.state==='processing');this.processingProgress=running?running.progress||0:0
        if(this.processingPending&&!status.workerAlive)bridge.native.resumeProcessing()
        if(!this.busy){const count=await bridge.native.publishProcessing(status.jobs);if(count&&id===this.sessionId){await this.refreshGallery();if(this.panel==='gallery')this.loadThumbnails();this.message('后台增强完成 '+count+' 张')}}
      }catch(e){}finally{this._processingPolling=false}
    },
    cyclePhotoProfile(){
      if(this.orientation==='portrait')return this.message('照片比例仅在横屏下可调')
      if(this.busy||this.recording)return this.message('请先结束当前拍摄')
      const list=this.photoProfileList
      if(!list.length)return this.message('设备未提供照片比例')
      const ix=list.findIndex(p=>p.id===this.photoProfile)
      const next=list[(ix+1)%list.length].id
      this.applyPhotoProfile(next)
    },
    async applyPhotoProfile(id){
      if(this.orientation==='portrait')return this.message('照片比例仅在横屏下可调')
      if(this.busy||this.recording)return this.message('请先结束当前拍摄')
      if(!this.connected)return this.message('相机尚未连接')
      if(!this.bridge)return
      try{
        const profile=await this.bridge.setPhotoProfile(id)
        this.photoProfile=id
        this.persist()
        if(profile)this.message('照片比例 '+profile.width+'×'+profile.height)
      }catch(e){this.message(e.message)}
    },
    async applyVideoProfile(id){
      if(this.busy||this.recording)return this.message('请先结束当前拍摄')
      if(!this.connected||!this.bridge)return this.message('相机尚未连接')
      try{
        const profile=await this.bridge.setVideoProfile(id)
        if(!profile)return this.message('此设备未提供录像画质选择')
        this.videoProfile=id;this.persist()
        this.message(profile.width+'×'+profile.height+' · 目标 '+profile.targetFps+' 帧/秒')
      }catch(e){this.message(e.message)}
    },
    toggleAutoSelfie(){this.autoSelfie=!this.autoSelfie;this.persist();if(this.autoSelfie)this.startSelfieSensor();else{if(this._selfieSource==='sensor')this.leaveSelfie();this.startSelfieSensor()}},
    enterSelfie(source='manual'){
      if(this.busy||this.recording||!this.connected){if(source==='manual')this.message('请先等待当前拍摄完成');return}
      if(this.selfieActive)return
      if(this.bridge.native.setCaptureMode)this.bridge.native.setCaptureMode('photo')
      this._modeBeforeSelfie=this.mode;this.mode='photo';this.panel='camera';this.selfieActive=true;this._selfieSource=source
      this._doubleTap=new DoubleTap();this.selfieHint=true
      clearTimeout(this._selfieHintTimer);this._selfieHintTimer=setTimeout(()=>{this.selfieHint=false},2500)
    },
    leaveSelfie(){
      this.selfieActive=false;this.selfieHint=false;this.selfieFlash=false;this._selfieSource=null
      if(this._modeBeforeSelfie){this.mode=this._modeBeforeSelfie;this._modeBeforeSelfie=null;try{if(this.bridge&&this.bridge.native.setCaptureMode)this.bridge.native.setCaptureMode(this.mode)}catch(e){}}
      if(this._doubleTap)this._doubleTap.reset()
      clearTimeout(this._selfieHintTimer);clearTimeout(this._flashTimer)
    },
    startSelfieSensor(){
      this.stopSelfieSensor()
      if(!this.bridge||typeof this.bridge.native.getOrientationSensor!=='function')return
      try{if(this.bridge.native.startOrientationSensor&&this.bridge.native.startOrientationSensor()===false){this.sensorAvailable=false;return}}catch(e){this.sensorAvailable=false;return}
      if(!this._flipTracker)this._flipTracker=new FlipTracker()
      if(!this._orientationTracker)this._orientationTracker=new OrientationTracker()
      this._flipTracker.flipped=false;this._flipTracker.candidate=null
      const read=()=>{
        if(!this._visible||!this.connected||this.busy||this.recording)return
        try{
          const value=this.bridge.native.getOrientationSensor();this.sensorAvailable=value.available===true
          const rotation=this._orientationTracker.sample(value,Date.now())
          if(rotation!==null&&this.layoutMode==='auto'){
            const previous=this.orientation
            this.rotation=rotation;this.detectedOrientation=rotation%180?'portrait':'landscape'
            this.applyOrientation()
            if(this.panel==='gallery'){if(previous!==this.orientation)this.galleryRow=0;this.loadThumbnails()}
          }
          const change=this.autoSelfie&&this.panel==='camera'?this._flipTracker.sample(value,Date.now()):null
          if(change!==null)console.warn('[snapphoto] flip '+JSON.stringify({active:change,baseline:this._flipTracker.baseline,current:value}))
          if(change===true)this.enterSelfie('sensor')
          else if(change===false&&this._selfieSource==='sensor')this.leaveSelfie()
        }catch(e){this.sensorAvailable=false;this.stopSelfieSensor()}
      }
      read();this._selfieSensorTimer=setInterval(read,250)
    },
    stopSelfieSensor(){
      if(this._selfieSensorTimer)clearInterval(this._selfieSensorTimer);this._selfieSensorTimer=null
      try{if(this.bridge&&this.bridge.native&&this.bridge.native.stopOrientationSensor)this.bridge.native.stopOrientationSensor()}catch(e){}
    },
    setTimer(n){if(this.busy)return;this.timerDelay=n;this.persist()},
    setBurst(n){if(this.busy)return;this.burstCount=n;this.persist()},
    toggleGrid(){this.showGrid=!this.showGrid;this.persist()},
    applyOrientation(){
      if(this.bridge&&this.bridge.native.setOrientation)this.bridge.native.setOrientation(this.orientation,captureRotation(this.layoutMode,this.orientation,this.rotation))
    },
    cycleLayout(){
      if(this.busy||this.recording)return this.message('请先结束当前拍摄')
      const previous=this.orientation
      const modes=['auto','portrait','landscape']
      this.layoutMode=modes[(modes.indexOf(this.layoutMode)+1)%modes.length]
      if(this.layoutMode==='auto'&&this._orientationTracker){this.rotation=this._orientationTracker.rotation;this.detectedOrientation=this.rotation%180?'portrait':'landscape'}
      this.applyOrientation()
      if(this.panel==='gallery'){if(previous!==this.orientation)this.galleryRow=0;this.loadThumbnails()}
      this.persist()
    },
    async connect(){
      const epoch=this._visibleEpoch
      if(this._connectWork)await this._connectWork
      if(!this._visible||epoch!==this._visibleEpoch)return
      const job=this.openCamera(epoch)
      this._connectWork=job
      try{await job}finally{if(this._connectWork===job)this._connectWork=null}
    },
    async openCamera(epoch){
      this.connecting=true
      await this.cleanup()
      if(!this._visible||epoch!==this._visibleEpoch){this.connecting=false;return}
      const id=++this.sessionId
      const bridge=createCameraBridge()
      this.bridge=bridge
      this.caps=null;this.preview='';this.connected=false;this.status='检查硬件接口…'
      try{
        const caps=await bridge.connect()
        if(id!==this.sessionId||!this._visible){await bridge.close();return}
        this.caps=caps
        this.connected=true;this.zoom=this.caps.minZoom;this.status='相机已连接'
        // A temporarily unavailable backend must not rewrite stored preferences.
        if(!caps.hdr)this.hdrMode='off'
        if(!caps.enhancement)this.denoiseMode='off'
        this.applyOrientation()
        if(bridge.native.setNight)bridge.native.setNight(this.nightMode,this.nightPriority)
        if(this.caps.zoom)await bridge.setZoom(this.zoom)
        await bridge.setFilter(this.filter)
        if(caps.videoProfiles&&caps.videoProfiles.length){
          if(!caps.videoProfiles.find(p=>p.id===this.videoProfile))this.videoProfile=caps.videoProfiles[0].id
          await bridge.setVideoProfile(this.videoProfile)
        }
        await bridge.setCaptureMode(this.mode)
        try{await bridge.setPhotoProfile(this.photoProfile)}catch(e){}
        if(id!==this.sessionId||!this._visible){await bridge.close();return}
        if(this.caps.preview){
          await bridge.openPreview()
          if(id!==this.sessionId||!this._visible){await bridge.close();return}
          this.poll()
          this.startSelfieSensor()
          if(caps.backgroundEnhancement){bridge.native.resumeProcessing();this.pollProcessing();this._processingTimer=setInterval(()=>this.pollProcessing(),2000)}
        }
        else this.status='缺少取景接口'
        if(this.caps.gallery){await this.refreshGallery();if(this.panel==='gallery')this.loadThumbnails()}
      }catch(e){if(id===this.sessionId&&this._visible){this.status=e.message;this.message(e.message)}}
      finally{this.connecting=false;this.updateOrientation()}
    },
    poll(){
      if(this.frameTimer)clearInterval(this.frameTimer)
      const id=this.sessionId,bridge=this.bridge
      let pending=false,updates=0,measuredAt=Date.now()
      this.previewUpdates=0;this.sourceFps=0
      this.frameTimer=setInterval(async()=>{
        if(pending||!this.connected||(this.panel!=='camera'&&!this.recording)||id!==this.sessionId||!this._visible)return
        pending=true
        try{
          const frame=await bridge.getPreviewFrame()
          if(id!==this.sessionId||!this._visible)return
          if(frame&&this.panel==='camera'){this.preview=frame;updates++}
          const now=Date.now(),info=bridge.frameInfo||{}
          if(now-measuredAt>=1000){this.previewUpdates=Math.round(updates*1000/(now-measuredAt));this.sourceFps=Math.round(info.previewFps||0);updates=0;measuredAt=now}
          this.nightActive=info.nightActive===true
          if(info.recordError&&this.recording&&!this.busy){this.message(info.recordLimit?'已达到文件或空间限制，正在保存':'录音或存储异常，正在结束录像');this.shutter()}
        }
        catch(e){if(id===this.sessionId&&this._visible){this.status='取景失败：'+e.message;clearInterval(this.frameTimer);this.frameTimer=null}}
        finally{pending=false}
      },33)
    },
    async changeMode(mode){
      if(this.busy||this.recording)return this.message('请先结束当前拍摄')
      if(mode==='video'&&!(this.caps&&this.caps.video))return this.message('设备未开放录像接口')
      try{if(this.bridge)await this.bridge.setCaptureMode(mode);this.mode=mode;await this.returnCamera()}catch(e){this.message(e.message)}
    },
    async shutter(){
      if(this.busy){
        if(this.mode==='photo'&&this.captureSession.running){this.captureSession.cancel();this.countdown=0;this.message('正在取消连拍或倒计时')}
        return
      }
      if(!this.connected)return this.message('未检测到 X7 Pro 相机接口')
      if(this.mode==='photo'&&!this.caps.photo)return this.message('设备未开放拍照接口')
      this.busy=true
      try{
        if(this.mode==='photo'){
          this.shotsCompleted=0
          this.lastGalleryWarning=''
          const result=await this.captureSession.run({
            delay:this.timerDelay,count:this.burstCount,
            onTick:n=>{this.countdown=n},
            capture:()=>this.bridge.capturePhoto({filter:this.filter,zoom:this.zoom,settleFocus:this.shotsCompleted===0,hdrMode:this.hdrMode,denoiseMode:this.denoiseMode,processingKeepAlive:this.processingKeepAlive,
              onProcessing:progress=>this.message('NPU 降噪 '+Math.round(progress)+'%')}),
            onShot:(result,now,total)=>{
              this.shotsCompleted=now
              if(result.galleryWarning)this.lastGalleryWarning=result.galleryWarning
              this.lastProcessing=processingLabel(result)
              this.status=(result.album==='pictures'?'已保存到相册':'文件已保存，未导入相册')+' '+now+'/'+total+' · '+this.fileName(result.path)
            }
          })
          if(result.completed>0){await this.refreshGallery();this.message(this.lastGalleryWarning||('已保存 '+result.completed+' 张 · '+this.lastProcessing))}
          else if(!result.cancelled)this.message('未拍摄照片')
        }else if(this.recording){
          const result=await this.bridge.stopRecording()
          if(this.recorderTimer)clearInterval(this.recorderTimer)
          this.recorderTimer=null;this.recording=false
          this.status=(result.album==='pictures'?'已保存到相册：':'录像已保存到其他目录：')+this.fileName(result.path)
          await this.refreshGallery();this.message(result.stopReason?'录像已保存（达到文件或空间限制）':(result.galleryWarning || '录像已保存到 /userdisk/Pictures'))
        }else{
          if(!this.caps.video)return this.message('设备未开放录像接口')
          await this.bridge.startRecording({filter:this.filter,zoom:this.zoom})
          this.recording=true;this.seconds=0
          this.recorderTimer=setInterval(()=>{this.seconds++},1000)
          this.message(this.caps.audio?'正在录像并录音':'正在录像')
        }
      }catch(e){
        if(this.recording&&this.bridge&&!this.bridge.recording){this.recording=false;clearInterval(this.recorderTimer);this.recorderTimer=null}
        this.message(e.message)
      }
      finally{this.busy=false;this.countdown=0}
    },
    async zoomBy(n){
      if(!this.caps||!this.caps.zoom)return this.message('设备未开放变焦接口')
      try{this.zoom=await this.bridge.setZoom(this.zoom+n)}
      catch(e){this.message(e.message)}
    },
    async focusAt(x,y){
      if(!this.caps||!this.caps.focus)return this.message('设备未开放对焦接口')
      try{await this.bridge.focusAt(x,y);this.status='已对焦'}
      catch(e){this.message(e.message)}
    },
    async chooseFilter(id){
      if(this.recording||this.busy)return this.message('拍摄中不可切换滤镜')
      if(!this.connected)return this.message('相机尚未连接')
      try{this.filter=await this.bridge.setFilter(id)}
      catch(e){this.message(e.message)}
    },
    async refreshGallery(){
      if(!this.caps||!this.caps.gallery){this.media=[];return}
      try{
        const previous=new Map(this.media.map(file=>[file.path,file]))
        this.media=(await this.bridge.listMedia()).map(file=>{
          const cached=previous.get(file.path)
          return cached&&cached.modified===file.modified&&cached.size===file.size
            ? {...file,thumbnail:file.thumbnail||cached.thumbnail}:file
        })
        const allowed=this.media.map(x=>x.path)
        this.selected=this.selected.filter(path=>allowed.indexOf(path)>=0)
      }catch(e){this.message('相册读取失败：'+e.message)}
    },
    async gallery(){
      if(this.recording||this.busy)return this.message('请先结束拍摄')
      try{
        if(this.bridge&&this.bridge.previewOpen){
          await this.bridge.closePreviewForPlayback()
          if(this.frameTimer)clearInterval(this.frameTimer);this.frameTimer=null;this.preview=''
        }
        this.panel='gallery';this.galleryRow=0;await this.refreshGallery();this.loadThumbnails()
      }catch(e){this.message('相册打开失败：'+e.message)}
    },
    loadThumbnails(){
      this._thumbnailEpoch=(this._thumbnailEpoch||0)+1
      // One worker replans after each completion. Scrolling cannot queue dozens
      // of obsolete jobs or discard a useful thumbnail that just finished.
      if(this._thumbnailWork)return this._thumbnailWork
      const bridge=this.bridge,attempted=new Set();let epoch=this._thumbnailEpoch
      const work=async()=>{
        while(this.panel==='gallery'&&this._visible&&this.bridge===bridge&&bridge&&bridge.native.thumbnail){
          if(epoch!==this._thumbnailEpoch){attempted.clear();epoch=this._thumbnailEpoch}
          const files=this.visibleMedia,window=galleryWindow(files.length,this.orientation,this.galleryRow,this.selecting)
          const first=window.row*window.columns
          const priority=files.slice(first,window.end).concat(files.slice(window.start,first))
          const file=priority.find(item=>!attempted.has(item.path))
          if(!file)return
          attempted.add(file.path)
          try{
            const r=await bridge.native.thumbnail(file.path)
            if(this.bridge!==bridge||!this._visible)return
            // Revalidate cached paths on each viewport visit: native LRU may
            // have evicted an old file while browsing a very large gallery.
            this.media=this.media.map(f=>f.path===file.path&&f.modified===file.modified&&f.size===file.size?{...f,thumbnail:r.path}:f)
          }catch(e){console.warn('[snapphoto] thumbnail '+e.message)}
        }
      }
      const pending=work().finally(()=>{
        if(this._thumbnailWork!==pending)return
        this._thumbnailWork=null
        if(this.bridge&&this.bridge!==bridge&&this.panel==='gallery'&&this._visible)this.loadThumbnails()
      })
      this._thumbnailWork=pending;return pending
    },
    galleryScroll(event){
      const offset=scrollOffset(event);if(offset===null)return
      const window=galleryWindow(this.visibleMedia.length,this.orientation,Math.floor(offset/(this.orientation==='portrait'?128:164)),this.selecting)
      if(window.row!==this.galleryRow){this.galleryRow=window.row;this.loadThumbnails()}
    },
    galleryTurn(step){this.galleryPage=Math.max(0,Math.min(Math.max(0,Math.ceil(this.visibleMedia.length/6)-1),this.galleryPage+step));this.loadThumbnails()},
    async openMedia(file){
      if(this.selecting)return this.toggle(file.path)
      this._thumbnailEpoch=(this._thumbnailEpoch||0)+1;this.mediaItem=file;this.mediaPreview=file.type==='photo'?'file://'+file.path:(file.thumbnail?'file://'+file.thumbnail:'');this.panel='media'
    },
    async mediaChange(step){const at=this.visibleMedia.findIndex(f=>this.mediaItem&&f.path===this.mediaItem.path);const file=this.visibleMedia[at+step];if(file){await this.stopPlayback();await this.openMedia(file)}},
    async importSelected(){
      if(this.busy)return
      const paths=this.media.filter(f=>f.type==='photo'&&this.selected.includes(f.path)).map(f=>f.path)
      if(!paths.length)return this.message('请先选择 JPEG 照片')
      this.busy=true
      try{await this.stopShare();const r=await this.bridge.native.importMedia(paths);this.selected=[];await this.refreshGallery();this.loadThumbnails();this.message('已导入 '+r.imported.length+' 张'+(r.failed.length?'，部分格式不支持或文件名冲突':''))}catch(e){this.message(e.message)}finally{this.busy=false}
    },
    async playMedia(){
      if(!this.mediaItem||this.mediaItem.type!=='video')return
      if(this.playing)return this.stopPlayback()
      try{
        await this.bridge.closePreviewForPlayback();this.bridge.native.playMedia(this.mediaItem.path,this.orientation);this.playing=true
        this._playTimer=setInterval(()=>{try{const r=this.bridge.native.playbackFrame();if(r.image)this.mediaPreview='file://'+r.image;if(!r.playing){this.playing=false;clearInterval(this._playTimer);this._playTimer=null;if(r.error)this.message(r.error)}}catch(e){this.stopPlayback();this.message(e.message)}},80)
      }catch(e){this.message('无法播放：'+e.message)}
    },
    async stopPlayback(){if(this._playTimer)clearInterval(this._playTimer);this._playTimer=null;this.playing=false;if(this.bridge&&this.bridge.native&&this.bridge.native.stopPlayback)this.bridge.native.stopPlayback()},
    async leaveMedia(){await this.stopPlayback();this.mediaItem=null;this.mediaPreview='';this.galleryRow=0;this.panel='gallery';this.loadThumbnails()},
    async returnCamera(){await this.stopPlayback();this.panel='camera';if(this.bridge&&!this.bridge.previewOpen){await this.bridge.openPreview();this.poll()}},
    requestDelete(){if(!this.selected.length)return this.message('请先选择文件');this.deleteConfirm=true},
    async deleteSelected(){
      if(this.busy)return
      const paths=this.selected.slice();this.deleteConfirm=false;this.busy=true
      try{await this.stopShare();const r=await this.bridge.native.deleteMedia(paths);this.selected=[];await this.refreshGallery();this.galleryPage=0;this.galleryRow=0;this.loadThumbnails();this.message('已删除 '+r.deleted.length+' 项'+(r.failed.length?'，部分文件未删除':''))}catch(e){this.message(e.message)}finally{this.busy=false}
    },
    cycleNight(){if(this.recording||this.busy)return;this.nightMode=this.nightMode==='auto'?'off':'auto';this.applyNight()},
    cycleNightPriority(){if(this.recording||this.busy)return;this.nightPriority=this.nightPriority==='speed'?'quality':'speed';this.applyNight()},
    applyNight(){try{if(this.bridge&&this.bridge.native.setNight)this.bridge.native.setNight(this.nightMode,this.nightPriority);this.persist()}catch(e){this.message(e.message)}},
    sharePanel(){this.panel='share'},
    settingsPanel(){this.panel='settings'},
    toggle(path){
      const i=this.selected.indexOf(path)
      if(i>=0)this.selected.splice(i,1)
      else this.selected.push(path)
    },
    selectAllVisible(){
      if(this.visibleMedia.length&&this.visibleMedia.every(f=>this.selected.includes(f.path))){const paths=this.visibleMedia.map(f=>f.path);this.selected=this.selected.filter(p=>!paths.includes(p));return}
      for(const f of this.visibleMedia)if(this.selected.indexOf(f.path)<0)this.selected.push(f.path)
    },
    clearSelection(){this.selected=[]},
    setTypeFilter(type){this.mediaTypeFilter=type;this.galleryPage=0;this.galleryRow=0;this.loadThumbnails()},
    async share(){
      if(!this.caps||!this.caps.share)return this.message('缺少局域网分享原生服务')
      if(this.shareStarting)return
      const files=this.chosenFiles.slice()
      if(!files.length)return this.message('请先拍摄并选择文件')
      this.shareStarting=true
      this.qr='';this.shareUrl='';this.stopShareCountdown()
      const id=this.sessionId,bridge=this.bridge
      try{
        const result=await bridge.startShare(files)
        if(id!==this.sessionId||!this._visible){await bridge.stopShare();return}
        this.shareUrl=result.url
        try{const png=makeQrImage(result.url);this.qr=this.bridge.native.persistQr?this.bridge.native.persistQr(png):png}
        catch(e){this.message('二维码绘制失败：'+e.message)}
        this.startShareCountdown(Number(result.expiresIn)||600)
      }catch(e){this.message(e.message)}
      finally{this.shareStarting=false}
    },
    startShareCountdown(seconds){
      this.stopShareCountdown()
      const total=Math.max(1,Math.floor(seconds)||600)
      this.shareExpiresAt=Date.now()+total*1000
      this.shareCountdown=total
      this._shareCountdownTimer=setInterval(()=>{
        const left=Math.max(0,Math.round((this.shareExpiresAt-Date.now())/1000))
        this.shareCountdown=left
        if(left<=0){this.stopShare().catch(()=>{});this.message('分享已过期')}
      },1000)
    },
    stopShareCountdown(){
      if(this._shareCountdownTimer)clearInterval(this._shareCountdownTimer)
      this._shareCountdownTimer=null;this.shareCountdown=0;this.shareExpiresAt=0
    },
    async stopShare(){
      try{if(this.bridge)await this.bridge.stopShare()}
      finally{this.shareUrl='';this.qr='';this.stopShareCountdown()}
    },
    fileName(path){return String(path||'').split('/').pop()},
    size(bytes){return formatBytes(bytes)},
    async cleanup(){
      const bridge=this.bridge
      this.sessionId++
      this._thumbnailEpoch=(this._thumbnailEpoch||0)+1;await this.stopPlayback();this.stopSelfieSensor();this.leaveSelfie();this.gestureCancel()
      this.recording=false;this.connected=false;this.shareUrl='';this.qr='';this.preview=''
      this.captureSession.cancel()
      this.countdown=0
      if(this.recorderTimer)clearInterval(this.recorderTimer)
      if(this.frameTimer)clearInterval(this.frameTimer)
      if(this.toastTimer)clearTimeout(this.toastTimer)
      if(this.orientationTimer)clearInterval(this.orientationTimer)
      if(this._processingTimer)clearInterval(this._processingTimer);this._processingTimer=null
      this.recorderTimer=null;this.frameTimer=null;this.toastTimer=null;this.orientationTimer=null
      this.stopShareCountdown()
      if(bridge){try{await bridge.close()}catch(e){console.log(e)}}
      if(this.bridge===bridge)this.bridge=null
    },
    async onShow(){
      this._visible=true
      const epoch=this._visibleEpoch=(this._visibleEpoch||0)+1
      await this.restoreSettings()
      if(!this._visible||epoch!==this._visibleEpoch)return
      this.updateOrientation()
      await this.connect()
      if(!this._visible||epoch!==this._visibleEpoch)return
      this.orientationTimer=setInterval(()=>{if(this.layoutMode==='auto')this.updateOrientation()},1500)
    },
    onHide(){this._visible=false;this._visibleEpoch=(this._visibleEpoch||0)+1;return this.cleanup()},
    onUnload(){return this.onHide()},
    exit(){this.cleanup();const page=this.$_page||this.$page;if(page)page.finish()}
  }
}
