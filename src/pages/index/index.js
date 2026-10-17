import {createCameraBridge} from '../../platform/camera-bridge.js'
import {FILTERS,duration,formatBytes} from '../../utils/format.js'
import {makeQrImage} from '../../utils/qr.js'
export default {
  name:'index',
  data(){return {
    bridge:null,caps:null,connected:false,busy:false,recording:false,seconds:0,recorderTimer:null,
    frameTimer:null,preview:'',zoom:1,mode:'photo',filter:'none',panel:'camera',
    media:[],selected:[],shareUrl:'',qr:'',status:'检测 X7 Pro 摄像头…',toast:'',toastTimer:null
  }},
  computed:{
    elapsed(){return duration(this.seconds)},
    filters(){return FILTERS.filter(f=>f.id==='none'||(this.caps&&this.caps.filters.indexOf(f.id)>=0))}
  },
  methods:{
    message(v){
      this.toast=String(v)
      if(this.toastTimer) clearTimeout(this.toastTimer)
      this.toastTimer=setTimeout(()=>{this.toast=''},3000)
    },
    async connect(){
      await this.cleanup()
      this.bridge=createCameraBridge()
      this.caps=null;this.preview='';this.connected=false;this.status='检查硬件接口…'
      try{
        this.caps=await this.bridge.connect()
        this.connected=true;this.zoom=this.caps.minZoom;this.status='相机已连接'
        if(this.caps.preview){await this.bridge.openPreview();this.poll()}
        else this.status='缺少取景接口'
        if(this.caps.gallery) await this.refreshGallery()
      }catch(e){this.status=e.message;this.message(e.message)}
    },
    poll(){
      if(this.frameTimer)clearInterval(this.frameTimer)
      let pending=false
      this.frameTimer=setInterval(async()=>{
        if(pending||!this.connected||this.panel!=='camera')return
        pending=true
        try {const frame=await this.bridge.getPreviewFrame(); if(frame) this.preview=frame}
        catch(e){this.status='取景失败：'+e.message;clearInterval(this.frameTimer);this.frameTimer=null}
        finally {pending=false}
      },500)
    },
    changeMode(mode){
      if(this.recording)return this.message('请先结束录像')
      if(mode==='video'&&!(this.caps&&this.caps.video))return this.message('设备不支持录像')
      this.mode=mode;this.panel='camera'
    },
    async shutter(){
      if(this.busy)return
      if(!this.connected)return this.message('未检测到 X7 Pro 相机接口')
      this.busy=true
      try{
        if(this.mode==='photo'){
          const result=await this.bridge.capturePhoto({filter:this.filter,zoom:this.zoom})
          this.status='保存成功：'+this.fileName(result.path)
          await this.refreshGallery();this.message('照片已保存')
        }else if(this.recording){
          const result=await this.bridge.stopRecording()
          clearInterval(this.recorderTimer);this.recorderTimer=null;this.recording=false
          this.status='保存成功：'+this.fileName(result.path)
          await this.refreshGallery();this.message('录像已保存')
        }else{
          await this.bridge.startRecording({filter:this.filter,zoom:this.zoom})
          this.recording=true;this.seconds=0
          this.recorderTimer=setInterval(()=>{this.seconds++},1000)
          this.message('正在录像')
        }
      }catch(e){this.message(e.message)}
      finally{this.busy=false}
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
      if(this.recording)return this.message('录像中不可切换滤镜')
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
    toggle(path){
      const i=this.selected.indexOf(path)
      if(i>=0)this.selected.splice(i,1)
      else this.selected.push(path)
    },
    async share(){
      if(!this.caps||!this.caps.share)return this.message('缺少局域网分享原生服务')
      const files=this.selected.length?this.selected.slice():this.media.map(x=>x.path)
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
      if(this.recorderTimer)clearInterval(this.recorderTimer)
      if(this.frameTimer)clearInterval(this.frameTimer)
      if(this.toastTimer)clearTimeout(this.toastTimer)
      this.recorderTimer=null;this.frameTimer=null;this.toastTimer=null
      if(this.bridge){try{await this.bridge.close()}catch(e){console.log(e)}}
      this.recording=false;this.connected=false;this.shareUrl='';this.qr=''
    },
    onShow(){this.connect()},
    onHide(){this.cleanup()},
    onUnload(){this.cleanup()},
    exit(){this.cleanup();if(this.$page)this.$page.finish()}
  }
}
