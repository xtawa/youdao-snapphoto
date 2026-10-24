import {createCameraBridge} from '../../platform/camera-bridge.js'
import {HDR_MODES,DENOISE_MODES,processingLabel} from '../../platform/processing-policy.js'
import {FILTERS,duration,formatBytes} from '../../utils/format.js'
import {makeQrImage} from '../../utils/qr.js'
import {CaptureSession,TIMER_CHOICES,BURST_CHOICES} from '../../utils/capture-session.js'
import {detectOrientation,effectiveOrientation} from '../../utils/orientation.js'
import {loadPreferences,savePreferences} from '../../utils/preferences.js'

export default {
  name:'index',
  data(){return {
    bridge:null,caps:null,connected:false,busy:false,recording:false,seconds:0,recorderTimer:null,
    frameTimer:null,orientationTimer:null,preview:'',zoom:1,mode:'photo',filter:'none',panel:'camera',
    media:[],selected:[],shareUrl:'',qr:'',status:'检测 X7 Pro 摄像头…',toast:'',toastTimer:null,
    timerChoices:TIMER_CHOICES,burstChoices:BURST_CHOICES,
    captureSession:new CaptureSession(),countdown:0,shotsCompleted:0,
    timerDelay:0,burstCount:1,showGrid:false,layoutMode:'auto',detectedOrientation:'landscape',
    mediaTypeFilter:'all',connecting:false,sessionId:0,lastGalleryWarning:'',
    hdrMode:'off',denoiseMode:'off',hdrModes:HDR_MODES,denoiseModes:DENOISE_MODES,
    lastProcessing:'',captureNotes:''
  }},
  computed:{
    elapsed(){return duration(this.seconds)},
    filters(){return FILTERS.filter(f=>f.id==='none'||(this.caps&&this.caps.filters.indexOf(f.id)>=0))},
    orientation(){return effectiveOrientation(this.layoutMode,this.detectedOrientation)},
    visibleMedia(){
      if(this.mediaTypeFilter==='all')return this.media
      return this.media.filter(f=>f.type===this.mediaTypeFilter)
    },
    chosenFiles(){return this.selected.length?this.selected:this.media.map(m=>m.path)},
    hdrAvailable(){return Boolean(this.caps && this.caps.hdr)},
    enhanceAvailable(){return Boolean(this.caps && this.caps.enhancement)},
    npuReady(){return Boolean(this.caps && this.caps.npuReady)},
    hdrLabel(){return this.hdrMode==='off'?'关':(this.hdrMode==='auto'?'自动':'开')},
    denoiseLabel(){return this.denoiseMode==='off'?'关':(this.denoiseMode==='auto'?'自动':'画质优先')},
    layoutLabel(){return this.layoutMode==='auto'?'自动':(this.layoutMode==='portrait'?'竖屏':'横屏')}
  },
  methods:{
    message(v){
      this.toast=String(v)
      if(this.toastTimer)clearTimeout(this.toastTimer)
      this.toastTimer=setTimeout(()=>{this.toast=''},3000)
    },
    updateOrientation(){
      let detected=detectOrientation()
      const native=this.bridge&&this.bridge.native
      if(native&&typeof native.getDisplaySize==='function'){
        Promise.resolve(native.getDisplaySize()).then(value=>{
          if(value&&Number(value.width)>0&&Number(value.height)>0){
            this.detectedOrientation=Number(value.height)>Number(value.width)?'portrait':'landscape'
          }
        }).catch(()=>{})
      }else this.detectedOrientation=detected
    },
    async restoreSettings(){
      const p=await loadPreferences()
      this.timerDelay=p.timerDelay;this.burstCount=p.burstCount
      this.showGrid=p.showGrid;this.layoutMode=p.layoutMode
      this.hdrMode=p.hdrMode;this.denoiseMode=p.denoiseMode
      this.updateOrientation()
    },
    persist(){
      savePreferences({timerDelay:this.timerDelay,burstCount:this.burstCount,
        showGrid:this.showGrid,layoutMode:this.layoutMode,
        hdrMode:this.hdrMode,denoiseMode:this.denoiseMode}).catch(()=>{})
    },
    setTimer(n){if(this.busy)return;this.timerDelay=n;this.persist()},
    setBurst(n){if(this.busy)return;this.burstCount=n;this.persist()},
    toggleGrid(){this.showGrid=!this.showGrid;this.persist()},
    setHDR(mode){
      if(this.busy||this.recording)return this.message('拍摄过程中不可切换 HDR')
      if(mode!=='off'&&!this.hdrAvailable)return this.message('X7 Pro 暂未开放可验证的 HDR 摄像头接口')
      this.hdrMode=mode;this.persist()
    },
    cycleHDR(){
      const next=this.hdrModes[(this.hdrModes.indexOf(this.hdrMode)+1)%this.hdrModes.length]
      this.setHDR(next)
    },
    setDenoise(mode){
      if(this.busy||this.recording)return this.message('拍摄过程中不可切换增强')
      if(mode!=='off'&&!this.enhanceAvailable)return this.message('图像增强原生接口尚未就绪')
      this.denoiseMode=mode;this.persist()
    },
    cycleDenoise(){
      const next=this.denoiseModes[(this.denoiseModes.indexOf(this.denoiseMode)+1)%this.denoiseModes.length]
      this.setDenoise(next)
    },
    cycleLayout(){
      const modes=['auto','portrait','landscape']
      this.layoutMode=modes[(modes.indexOf(this.layoutMode)+1)%modes.length]
      this.persist()
    },
    async connect(){
      if(this.connecting)return
      this.connecting=true
      await this.cleanup()
      const id=++this.sessionId
      this.bridge=createCameraBridge()
      this.caps=null;this.preview='';this.connected=false;this.status='检查硬件接口…'
      try{
        this.caps=await this.bridge.connect()
        if(id!==this.sessionId)return
        this.connected=true;this.zoom=this.caps.minZoom;this.status='相机已连接'
        if(this.hdrMode!=='off'&&!this.caps.hdr){this.hdrMode='off';this.persist()}
        if(this.denoiseMode!=='off'&&!this.caps.enhancement){this.denoiseMode='off';this.persist()}
        if(this.caps.preview){await this.bridge.openPreview();this.poll()}
        else this.status='缺少取景接口'
        if(this.caps.gallery)await this.refreshGallery()
      }catch(e){this.status=e.message;this.message(e.message)}
      finally{this.connecting=false;this.updateOrientation()}
    },
    poll(){
      if(this.frameTimer)clearInterval(this.frameTimer)
      let pending=false
      this.frameTimer=setInterval(async()=>{
        if(pending||!this.connected||this.panel!=='camera')return
        pending=true
        try{const frame=await this.bridge.getPreviewFrame();if(frame)this.preview=frame}
        catch(e){this.status='取景失败：'+e.message;clearInterval(this.frameTimer);this.frameTimer=null}
        finally{pending=false}
      },500)
    },
    changeMode(mode){
      if(this.busy||this.recording)return this.message('请先结束当前拍摄')
      if(mode==='video'&&!(this.caps&&this.caps.video))return this.message('设备未开放录像接口')
      this.mode=mode;this.panel='camera'
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
            capture:()=>this.bridge.capturePhoto({filter:this.filter,zoom:this.zoom,hdrMode:this.hdrMode,denoiseMode:this.denoiseMode}),
            onShot:(result,now,total)=>{
              this.shotsCompleted=now
              if(result.galleryWarning)this.lastGalleryWarning=result.galleryWarning
              this.lastProcessing=processingLabel(result)
              this.status=(result.album==='pictures'?'已保存到相册':'文件已保存，未导入相册')+' '+now+'/'+total+' · '+this.fileName(result.path)
            }
          })
          if(result.completed>0){await this.refreshGallery();this.message(this.lastGalleryWarning || ('已保存 '+result.completed+' 张 · '+this.lastProcessing))}
          else if(!result.cancelled)this.message('未拍摄照片')
        }else if(this.recording){
          const result=await this.bridge.stopRecording()
          if(this.recorderTimer)clearInterval(this.recorderTimer)
          this.recorderTimer=null;this.recording=false
          this.status=(result.album==='pictures'?'已保存到相册：':'录像已保存到其他目录：')+this.fileName(result.path)
          await this.refreshGallery();this.message(result.galleryWarning || '录像已保存到 /userdisk/Pictures')
        }else{
          if(!this.caps.video)return this.message('设备未开放录像接口')
          await this.bridge.startRecording({filter:this.filter,zoom:this.zoom})
          this.recording=true;this.seconds=0
          this.recorderTimer=setInterval(()=>{this.seconds++},1000)
          this.message('正在录像')
        }
      }catch(e){this.message(e.message)}
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
        this.media=await this.bridge.listMedia()
        const allowed=this.media.map(x=>x.path)
        this.selected=this.selected.filter(path=>allowed.indexOf(path)>=0)
      }catch(e){this.message('相册读取失败：'+e.message)}
    },
    async gallery(){this.panel='gallery';await this.refreshGallery()},
    sharePanel(){this.panel='share'},
    settingsPanel(){this.panel='settings'},
    toggle(path){
      const i=this.selected.indexOf(path)
      if(i>=0)this.selected.splice(i,1)
      else this.selected.push(path)
    },
    selectAllVisible(){
      for(const f of this.visibleMedia)if(this.selected.indexOf(f.path)<0)this.selected.push(f.path)
    },
    clearSelection(){this.selected=[]},
    setTypeFilter(type){this.mediaTypeFilter=type},
    async share(){
      if(!this.caps||!this.caps.share)return this.message('缺少局域网分享原生服务')
      const files=this.chosenFiles.slice()
      if(!files.length)return this.message('请先拍摄并选择文件')
      try{
        const result=await this.bridge.startShare(files)
        this.shareUrl=result.url
        try{this.qr=makeQrImage(result.url)}
        catch(e){this.message('二维码绘制失败：'+e.message)}
      }catch(e){this.message(e.message)}
    },
    async stopShare(){
      try{if(this.bridge)await this.bridge.stopShare()}
      finally{this.shareUrl='';this.qr=''}
    },
    fileName(path){return String(path||'').split('/').pop()},
    size(bytes){return formatBytes(bytes)},
    async cleanup(){
      this.sessionId++
      this.captureSession.cancel()
      this.countdown=0
      if(this.recorderTimer)clearInterval(this.recorderTimer)
      if(this.frameTimer)clearInterval(this.frameTimer)
      if(this.toastTimer)clearTimeout(this.toastTimer)
      if(this.orientationTimer)clearInterval(this.orientationTimer)
      this.recorderTimer=null;this.frameTimer=null;this.toastTimer=null;this.orientationTimer=null
      if(this.bridge){try{await this.bridge.close()}catch(e){console.log(e)}}
      this.recording=false;this.connected=false;this.shareUrl='';this.qr='';this.preview=''
    },
    async onShow(){
      await this.restoreSettings()
      this.updateOrientation()
      await this.connect()
      this.orientationTimer=setInterval(()=>{if(this.layoutMode==='auto')this.updateOrientation()},1500)
    },
    onHide(){this.cleanup()},
    onUnload(){this.cleanup()},
    exit(){this.cleanup();if(this.$page)this.$page.finish()}
  }
}
