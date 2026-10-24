/* Still-photo jobs only. The original JPEG is never changed or removed. */
#include "rknn/npu_bridge.h"
#include "rknn/tiled_rgb.h"
static pthread_t enhance_thread;
static pthread_mutex_t enhance_lock=PTHREAD_MUTEX_INITIALIZER;
static int enhance_phase,enhance_cancel,enhance_progress;
static cJSON *enhance_result;
static char enhance_source[4096],enhance_mode[16],npu_model[4096];
static struct timespec enhance_time;
static int npu_checked,npu_verified;
static SnapNpuInfo npu_info;
static void (*enhance_progress_hook)(int);
static int bundled_npu_model(void){
 if(npu_model[0])return 1;
#ifdef SNAP_CLI
 const char *override=getenv("SNAPPHOTO_MODEL");
 if(override&&strlen(override)<sizeof(npu_model)){snprintf(npu_model,sizeof(npu_model),"%s",override);return 1;}
#endif
 Dl_info module;char location[4096];
 if(!dladdr((void*)bundled_npu_model,&module)||!module.dli_fname||!realpath(module.dli_fname,location))return 0;
 char *end=strrchr(location,'/');if(!end)return 0;*end=0;
 /* Falcon extracts native modules into <slot>/libs, assets into <slot>. */
 end=strrchr(location,'/');if(!end||strcmp(end,"/libs"))return 0;*end=0;
 if(snprintf(npu_model,sizeof(npu_model),"%s/assets/models/ffdnet-color-rk3562.rknn",location)>=(int)sizeof(npu_model)){npu_model[0]=0;return 0;}
 return 1;
}
static int npu_check(void){
 if(npu_checked)return npu_verified;
 npu_checked=1;
 if(!bundled_npu_model()||!snap_npu_open(npu_model,0,&npu_info))return 0;
 unsigned char *input=malloc(256*256*3),*output=malloc(256*256*3);
 if(input&&output){memset(input,128,256*256*3);npu_verified=snap_npu_tile(input,256*3,12,output,&npu_info);}
 free(input);free(output);snap_npu_close();return npu_verified;
}
static unsigned char *enhance_decode(const char *path,int *w,int *h){
 if(!load_jpeg())return NULL;
 tjhandle (*init)(void)=dlsym(jpeg_lib,"tjInitDecompress");
 int (*header)(tjhandle,const unsigned char*,unsigned long,int*,int*,int*,int*)=dlsym(jpeg_lib,"tjDecompressHeader3");
 int (*decode)(tjhandle,const unsigned char*,unsigned long,unsigned char*,int,int,int,int,int)=dlsym(jpeg_lib,"tjDecompress2");
 FILE *f=NULL;tjhandle codec=NULL;unsigned char *data=NULL,*rgb=NULL;int good=0,sub,color;
 if(!init||!header||!decode)goto done;
 f=fopen(path,"rb");if(!f||fseek(f,0,SEEK_END))goto done;long size=ftell(f);
 if(size<4||size>64*1024*1024L||fseek(f,0,SEEK_SET))goto done;
 data=malloc(size);if(!data||fread(data,1,size,f)!=(size_t)size)goto done;
 fclose(f);f=NULL;codec=init();
 if(!codec||header(codec,data,size,w,h,&sub,&color)||*w<1||*h<1||*w>8192||*h>8192||(size_t)*w**h>12*1024*1024)goto done;
 rgb=malloc((size_t)*w**h*3);if(!rgb||decode(codec,data,size,rgb,*w,0,*h,TJPF_RGB,TJFLAG_ACCURATEDCT))goto done;
 good=1;
done:
 if(codec)jpeg_destroy(codec);if(f)fclose(f);free(data);if(!good){free(rgb);rgb=NULL;}return rgb;
}
/* Robust high-pass noise estimate: discard strong edges with a histogram
 * median rather than mistaking text/detail for Gaussian noise. */
static int enhance_noise(const unsigned char *rgb,int w,int h,double *luma){
 unsigned hist[256]={0},count=0;double light=0;
 for(int y=1;y<h-1;y+=3)for(int x=1;x<w-1;x+=3){
  size_t p=((size_t)y*w+x)*3;
  int c=(rgb[p]*77+rgb[p+1]*150+rgb[p+2]*29)>>8;
  int a=0;const int offsets[]={-3,3,-w*3,w*3};
  for(int k=0;k<4;k++){size_t at=p+offsets[k];a+=(rgb[at]*77+rgb[at+1]*150+rgb[at+2]*29)>>8;}
  int value=abs(c*4-a)/4;if(value>255)value=255;hist[value]++;light+=c;count++;
 }
 if(!count){*luma=0;return 6;}*luma=light/count;unsigned sum=0;int median=0;
 for(;median<255;median++){sum+=hist[median];if(sum>=count/2)break;}
 /* Luma noise is attenuated relative to the per-RGB-channel model sigma. */
 int sigma=(int)lround(median*2.0);return sigma<4?4:sigma>18?18:sigma;
}
static double enhance_clock(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;}
static int enhance_stopped(double began){pthread_mutex_lock(&enhance_lock);int cancel=enhance_cancel;pthread_mutex_unlock(&enhance_lock);return cancel||enhance_clock()-began>120000;}
static void *enhance_main(void *unused){
 (void)unused;double began=enhance_clock(),hardware_us=0,luma=0;int w=0,h=0,tiles=0,sigma=0;
 unsigned char *source=enhance_decode(enhance_source,&w,&h),*result=NULL,*input=NULL,*output=NULL;
 cJSON *receipt=NULL;char destination[256]={0},partial[280]={0};SnapNpuInfo info={0};
 if(!source){receipt=fail("Cannot decode a bounded JPEG for NPU enhancement");goto done;}
 sigma=enhance_noise(source,w,h,&luma);
 if(!strcmp(enhance_mode,"auto")&&sigma<6){receipt=saved(enhance_source);cJSON_AddStringToObject(receipt,"skipReason","low-noise");cJSON_AddBoolToObject(receipt,"npuUsed",0);goto done;}
 if(!snap_npu_open(npu_model,0,&info)){receipt=fail(info.error);goto done;}
 result=malloc((size_t)w*h*3);input=malloc(256*256*3);output=malloc(256*256*3);
 if(!result||!input||!output){receipt=fail("Insufficient memory for bounded NPU photo job");goto done;}
 int total=((w+SNAP_CORE-1)/SNAP_CORE)*((h+SNAP_CORE-1)/SNAP_CORE);
 for(int y=0;y<h;y+=SNAP_CORE)for(int x=0;x<w;x+=SNAP_CORE){
  if(enhance_stopped(began)){receipt=fail("NPU photo job cancelled or timed out; original retained");goto done;}
  snap_extract_tile(source,w,h,x,y,input);
  if(!snap_npu_tile(input,256*3,sigma,output,&info)){receipt=fail(info.error);goto done;}
  snap_commit_tile(source,result,w,h,x,y,output,!strcmp(enhance_mode,"quality")?192:160);
  tiles++;hardware_us+=info.last_run_us;
  pthread_mutex_lock(&enhance_lock);enhance_progress=tiles*95/total;pthread_mutex_unlock(&enhance_lock);
  if(enhance_progress_hook)enhance_progress_hook(enhance_progress);
 }
 if(enhance_stopped(began)){receipt=fail("NPU photo job cancelled; original retained");goto done;}
 char prefix[40];snprintf(prefix,sizeof(prefix),"SNAP_AI_%d",getpid());filename(destination,prefix,"JPG",&enhance_time);snprintf(partial,sizeof(partial),"%s.partial",destination);
 /* Exclusive reservation avoids replacing any existing gallery file. */
 int reserve=open(destination,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
 if(reserve<0){destination[0]=0;receipt=fail("Enhanced filename already exists");goto done;}close(reserve);
 if(!jpeg_write(partial,result,w,h,94,&enhance_time)||enhance_stopped(began)||rename(partial,destination)){receipt=fail("Cannot commit enhanced JPEG; original retained");goto done;}
 receipt=saved(destination);cJSON_AddBoolToObject(receipt,"denoiseApplied",1);cJSON_AddBoolToObject(receipt,"npuUsed",tiles>0);
 cJSON_AddStringToObject(receipt,"sourcePath",enhance_source);cJSON_AddStringToObject(receipt,"npuBackend","rknn-ffdnet-int8");
 cJSON_AddStringToObject(receipt,"runtime",info.sdk);cJSON_AddStringToObject(receipt,"driver",info.driver);
 cJSON_AddNumberToObject(receipt,"tiles",tiles);cJSON_AddNumberToObject(receipt,"noiseSigma",sigma);cJSON_AddNumberToObject(receipt,"sceneLuma",luma);
 cJSON_AddNumberToObject(receipt,"width",w);cJSON_AddNumberToObject(receipt,"height",h);cJSON_AddNumberToObject(receipt,"npuTimeMs",hardware_us/1000);
done:
 snap_npu_close();free(source);free(result);free(input);free(output);
 if(partial[0])unlink(partial);
 if(destination[0]&&(!receipt||cJSON_GetObjectItem(receipt,"error")))unlink(destination);
 if(!receipt)receipt=fail("NPU photo job failed");cJSON_AddNumberToObject(receipt,"processingMs",enhance_clock()-began);
 pthread_mutex_lock(&enhance_lock);enhance_result=receipt;enhance_phase=2;pthread_mutex_unlock(&enhance_lock);return NULL;
}
static int enhance_busy(void){pthread_mutex_lock(&enhance_lock);int busy=enhance_phase!=0;pthread_mutex_unlock(&enhance_lock);return busy;}
static void enhance_shutdown(void){
 pthread_mutex_lock(&enhance_lock);int phase=enhance_phase;enhance_cancel=1;pthread_mutex_unlock(&enhance_lock);
 if(phase)pthread_join(enhance_thread,NULL);
 pthread_mutex_lock(&enhance_lock);cJSON_Delete(enhance_result);enhance_result=NULL;enhance_phase=0;pthread_mutex_unlock(&enhance_lock);snap_npu_close();
}
static cJSON *enhance_command(cJSON *o,const char *op){
 if(!strcmp(op,"enhanceStart")){
  if(enhance_busy()||photo_phase||avi)return fail("Camera processing busy");
  const char *mode=str(o,"mode");if(strcmp(mode,"auto")&&strcmp(mode,"quality"))return fail("Invalid enhancement mode");
  char real[4096];if(!valid_media(str(o,"path"),real)||strcmp(real,str(o,"path")))return fail("Only canonical gallery JPEGs may be enhanced");
  const char *ext=strrchr(real,'.');if(!ext||(strcasecmp(ext,".jpg")&&strcasecmp(ext,".jpeg")))return fail("Enhancement requires JPEG input");
  if(!npu_check())return fail("Bundled NPU model unavailable");
  snprintf(enhance_source,sizeof(enhance_source),"%s",real);snprintf(enhance_mode,sizeof(enhance_mode),"%s",mode);
  double stamp=cJSON_GetNumberValue(cJSON_GetObjectItem(o,"capturedAtEpochMs"));struct stat st;stat(real,&st);
  if(!isfinite(stamp)||stamp<946684800000.0||stamp>4102444800000.0)stamp=st.st_mtime*1000.0;
  enhance_time.tv_sec=(time_t)(stamp/1000);enhance_time.tv_nsec=(long)(fmod(stamp,1000)*1000000);
  pthread_mutex_lock(&enhance_lock);enhance_cancel=enhance_progress=0;enhance_phase=1;pthread_mutex_unlock(&enhance_lock);
  if(pthread_create(&enhance_thread,NULL,enhance_main,NULL)){pthread_mutex_lock(&enhance_lock);enhance_phase=0;pthread_mutex_unlock(&enhance_lock);return fail("Cannot start NPU worker");}return ok();
 }
 if(!strcmp(op,"enhanceResult")){
  pthread_mutex_lock(&enhance_lock);int phase=enhance_phase,progress=enhance_progress;cJSON *result=enhance_result;
  if(phase==2){enhance_result=NULL;enhance_phase=0;}pthread_mutex_unlock(&enhance_lock);
  if(phase==1){cJSON *j=ok();cJSON_AddBoolToObject(j,"pending",1);cJSON_AddNumberToObject(j,"progress",progress);return j;}
  if(phase==2){pthread_join(enhance_thread,NULL);return result?result:fail("NPU photo job failed");}return fail("No pending enhancement");
 }
 return NULL;
}
