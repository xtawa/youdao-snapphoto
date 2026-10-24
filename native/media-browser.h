/* Media operations are confined to validated, flat gallery files. Child
 * processes receive argv directly; filenames never become shell commands. */
static pthread_t thumb_thread;
static int thumb_phase,thumb_cancel;static cJSON *thumb_result;
static char thumb_source[4096],thumb_dest[256];
static pid_t playback_pid;static unsigned playback_generation;static int playback_status;
static char playback_prefix[100];
static char playback_last[256],playback_image[256];static unsigned playback_presented;
static void media_uri(const char *path,char *uri,size_t cap){
 size_t at=snprintf(uri,cap,"file://");
 for(const unsigned char *s=(const unsigned char*)path;*s&&at+4<cap;s++){
  if((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||strchr("/._-",*s))uri[at++]=*s;
  else{snprintf(uri+at,4,"%%%02X",*s);at+=3;}
 }
 uri[at]=0;
}
static int media_spawn(char *const argv[],pid_t *pid){
 posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
 posix_spawn_file_actions_addopen(&actions,0,"/dev/null",O_RDONLY,0);
 posix_spawn_file_actions_addopen(&actions,1,CACHE "/media.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
 posix_spawn_file_actions_adddup2(&actions,1,2);
 posix_spawnattr_t attrs;posix_spawnattr_init(&attrs);posix_spawnattr_setflags(&attrs,POSIX_SPAWN_SETPGROUP);posix_spawnattr_setpgroup(&attrs,0);
 int error=posix_spawnp(pid,argv[0],&actions,&attrs,argv,environ);posix_spawnattr_destroy(&attrs);
 posix_spawn_file_actions_destroy(&actions);return error==0;
}
#include "playback-frames.h"
static void thumb_cache_path(const char *path,char out[256]){
 struct stat st={0};stat(path,&st);uint64_t key=1469598103934665603ULL;
 for(const unsigned char*p=(const unsigned char*)path;*p;p++){key^=*p;key*=1099511628211ULL;}
 key^=(uint64_t)st.st_mtime;key*=1099511628211ULL;key^=(uint64_t)st.st_size;
 snprintf(out,256,CACHE "/thumb-v4-%016llx.jpg",(unsigned long long)key);
}
static void thumb_prune(void){
 DIR*d=opendir(CACHE);if(!d)return;struct dirent*e;int count=0;char oldest[256]={0};time_t age=LONG_MAX;
 while((e=readdir(d)))if(!strncmp(e->d_name,"thumb-",6)&&strstr(e->d_name,".jpg")){
  char path[256];snprintf(path,sizeof(path),CACHE "/%s",e->d_name);struct stat st;
  if(!stat(path,&st)){count++;if(st.st_mtime<age){age=st.st_mtime;snprintf(oldest,sizeof(oldest),"%s",path);}}
 }
 closedir(d);if(count>=256&&oldest[0])unlink(oldest);
}
/* JPEG photos use the installed TurboJPEG decoder's DCT scaling, not a new
 * GStreamer process/full sensor-size RGB allocation for every tile. Video and
 * other formats retain the bounded GStreamer fallback below. */
static int thumbnail_jpeg(const char *source,const char *destination){
 const char *ext=strrchr(source,'.');if(!ext||(strcasecmp(ext,".jpg")&&strcasecmp(ext,".jpeg")))return 0;
 void *lib=dlopen("libturbojpeg.so.0",RTLD_NOW|RTLD_LOCAL);if(!lib)return 0;
 tjhandle (*init)(void)=dlsym(lib,"tjInitDecompress");
 int (*header)(tjhandle,const unsigned char*,unsigned long,int*,int*,int*,int*)=dlsym(lib,"tjDecompressHeader3");
 tjscalingfactor *(*factors)(int*)=dlsym(lib,"tjGetScalingFactors");
 int (*decode)(tjhandle,const unsigned char*,unsigned long,unsigned char*,int,int,int,int,int)=dlsym(lib,"tjDecompress2");
 int (*destroy)(tjhandle)=dlsym(lib,"tjDestroy");
 tjhandle codec=NULL;FILE *file=NULL;unsigned char *jpeg=NULL,*rgb=NULL,*tile=NULL;int good=0;
 if(!init||!header||!factors||!decode||!destroy)goto done;
 file=fopen(source,"rb");if(!file||fseek(file,0,SEEK_END))goto done;
 long length=ftell(file);if(length<4||length>64*1024*1024L||fseek(file,0,SEEK_SET))goto done;
 jpeg=malloc(length);if(!jpeg||fread(jpeg,1,length,file)!=(size_t)length)goto done;
 fclose(file);file=NULL;codec=init();int w,h,sub,color;
 if(!codec||header(codec,jpeg,length,&w,&h,&sub,&color)||w<1||h<1||w>32768||h>32768)goto done;
 const int edge=160;int count=0,sw=w,sh=h;size_t best=(size_t)w*h;
 tjscalingfactor *scales=factors(&count);if(!scales)goto done;
 for(int i=0;i<count;i++){
  int x=TJSCALED(w,scales[i]),y=TJSCALED(h,scales[i]);size_t area=(size_t)x*y;
  if(x>=edge&&y>=edge&&area<=best){sw=x;sh=y;best=area;}
 }
 if(best>4*1024*1024)goto done;
 rgb=malloc(best*3);tile=malloc((size_t)edge*edge*3);if(!rgb||!tile)goto done;
 if(decode(codec,jpeg,length,rgb,sw,0,sh,TJPF_RGB,TJFLAG_FASTDCT|TJFLAG_FASTUPSAMPLE))goto done;
 int crop=sw<sh?sw:sh,x0=(sw-crop)/2,y0=(sh-crop)/2;
 for(int y=0;y<edge;y++)for(int x=0;x<edge;x++){
  int sx=x0+x*crop/edge,sy=y0+y*crop/edge;
  memcpy(tile+((size_t)y*edge+x)*3,rgb+((size_t)sy*sw+sx)*3,3);
 }
 good=jpeg_write(destination,tile,edge,edge,76,NULL);
done:
 if(file)fclose(file);if(codec)destroy(codec);free(jpeg);free(rgb);free(tile);dlclose(lib);return good;
}
static void *thumbnail_main(void *unused){
 (void)unused;char uri[12300],location[300],tmp[280];
 snprintf(tmp,sizeof(tmp),"%s.tmp",thumb_dest);
 if(thumbnail_jpeg(thumb_source,tmp)){
  pthread_mutex_lock(&lock);int cancelled=thumb_cancel;pthread_mutex_unlock(&lock);
  int good=!cancelled&&complete_jpeg(tmp)&&rename(tmp,thumb_dest)==0;
  cJSON*j=good?ok():fail("Thumbnail cancelled or cannot be saved");
  if(good)cJSON_AddStringToObject(j,"path",thumb_dest);else unlink(tmp);
  pthread_mutex_lock(&lock);thumb_result=j;thumb_phase=2;pthread_mutex_unlock(&lock);return NULL;
 }
 media_uri(thumb_source,uri,sizeof(uri));
 char source[12400];snprintf(source,sizeof(source),"uri=%s",uri);
 snprintf(tmp,sizeof(tmp),"%s.tmp",thumb_dest);snprintf(location,sizeof(location),"location=%s",tmp);
 char *argv[]={"timeout","8","gst-launch-1.0","-q","uridecodebin",source,"!","videoconvert","!","videoscale","!","video/x-raw,width=240,pixel-aspect-ratio=1/1","!","jpegenc","snapshot=true","quality=76","!","filesink","buffer-mode=unbuffered",location,NULL};
 pid_t pid;int status=1,good=media_spawn(argv,&pid),ended=0;long jpeg_end=0;double began=now_ms();
 if(good){
  while(now_ms()-began<7500){
   pthread_mutex_lock(&lock);int cancel=thumb_cancel;pthread_mutex_unlock(&lock);if(cancel)break;
   FILE *image=fopen(tmp,"rb");
   if(image){int previous=-1,current;long length=0;
    while(length<2*1024*1024&&(current=fgetc(image))!=EOF){length++;if(previous==255&&current==217){jpeg_end=length;break;}previous=current;}fclose(image);}
   if(jpeg_end)break;
   if(waitpid(pid,&status,WNOHANG)==pid){ended=1;break;}usleep(20000);
  }
  if(!ended){kill(-pid,SIGTERM);for(int i=0;i<30;i++){if(waitpid(pid,&status,WNOHANG)==pid){ended=1;break;}usleep(10000);}if(!ended){kill(-pid,SIGKILL);waitpid(pid,&status,0);}}
 }
 good=good&&jpeg_end>2&&truncate(tmp,jpeg_end)==0&&complete_jpeg(tmp)&&rename(tmp,thumb_dest)==0;
 cJSON*j=good?ok():fail("Cannot decode this media preview");if(good)cJSON_AddStringToObject(j,"path",thumb_dest);else unlink(tmp);
 pthread_mutex_lock(&lock);thumb_result=j;thumb_phase=2;pthread_mutex_unlock(&lock);return NULL;
}
static void thumbnail_join(void){
 pthread_mutex_lock(&lock);int phase=thumb_phase;thumb_cancel=1;pthread_mutex_unlock(&lock);
 if(phase){pthread_join(thumb_thread,NULL);pthread_mutex_lock(&lock);cJSON_Delete(thumb_result);thumb_result=NULL;thumb_phase=0;pthread_mutex_unlock(&lock);}
}
static void stop_playback(void){
 if(!playback_pid)return;
 kill(-playback_pid,SIGTERM);
 for(int i=0;i<20;i++){if(waitpid(playback_pid,&playback_status,WNOHANG)==playback_pid){playback_pid=0;return;}usleep(10000);}
 kill(-playback_pid,SIGKILL);waitpid(playback_pid,&playback_status,0);playback_pid=0;
}
static void playback_cleanup(void){
 DIR*d=opendir(CACHE);if(!d)return;struct dirent*e;
 while((e=readdir(d)))if(!strncmp(e->d_name,"play-",5)){
  char path[512];snprintf(path,sizeof(path),CACHE "/%s",e->d_name);unlink(path);
 }closedir(d);
}
static cJSON *media_listing(void){
 cJSON*a=cJSON_CreateArray();DIR*d=opendir(GALLERY);if(!d)return a;struct dirent*e;
 while((e=readdir(d))){
  int kind=media_kind(e->d_name);if(!kind)continue;char path[4096],real[4096];
  snprintf(path,sizeof(path),GALLERY "/%s",e->d_name);if(!valid_media(path,real))continue;
  struct stat st;if(stat(real,&st))continue;cJSON*j=ok();
  cJSON_AddStringToObject(j,"path",real);cJSON_AddStringToObject(j,"name",e->d_name);
  cJSON_AddStringToObject(j,"type",kind==2?"video":"photo");
  cJSON_AddNumberToObject(j,"size",st.st_size);cJSON_AddNumberToObject(j,"modified",st.st_mtime);
  char taken[32];if(captured_name(real,taken)){cJSON_AddStringToObject(j,"capturedAtLocal",taken);cJSON_AddStringToObject(j,"timeSource","filename-device-clock");}
  char thumb[256];thumb_cache_path(real,thumb);if(complete_jpeg(thumb))cJSON_AddStringToObject(j,"thumbnail",thumb);
  cJSON_AddItemToArray(a,j);
 }
 closedir(d);return a;
}
static cJSON *media_command(cJSON *o,const char *op){
 if(!strcmp(op,"publish")){
  char path[4096];if(!valid_media(str(o,"path"),path)||strcmp(path,str(o,"path")))return fail("Only canonical gallery photos may be published");
  const char *extension=strrchr(path,'.');if(!extension||strcasecmp(extension,".jpg"))return fail("Only JPEG photos can be imported into the stock album");
  if(strcmp(extension,".JPG")){
   char target[4096];snprintf(target,sizeof(target),"%.*s.JPG",(int)(extension-path),path);
   if(link(path,target))return fail("Album filename exists or cannot be created; original kept");
   if(unlink(path)){unlink(target);return fail("Original photo retained");}snprintf(path,sizeof(path),"%s",target);
  }
  cJSON*j=ok();cJSON_AddStringToObject(j,"path",path);cJSON_AddBoolToObject(j,"written",1);return j;
 }
 if(!strcmp(op,"delete")){
  if(avi||photo_phase)return fail("Stop capture before deleting media");
  cJSON*a=cJSON_GetObjectItem(o,"paths");int count=cJSON_GetArraySize(a);
  if(!cJSON_IsArray(a)||count<1||count>MAX_FILES)return fail("Select 1 to 256 files");
  char (*paths)[4096]=calloc(count,4096);if(!paths)return fail("Memory unavailable");
  for(int i=0;i<count;i++){
   cJSON *v=cJSON_GetArrayItem(a,i);
   if(!cJSON_IsString(v)||!valid_media(v->valuestring,paths[i])||strcmp(paths[i],v->valuestring)){free(paths);return fail("Only canonical gallery media may be deleted");}
   struct stat st;if(lstat(v->valuestring,&st)||!S_ISREG(st.st_mode)){free(paths);return fail("Refusing symlink or non-file");}
  }
  stop_playback();thumbnail_join();cJSON*j=ok(),*deleted=cJSON_CreateArray(),*failed=cJSON_CreateArray();
  for(int i=0;i<count;i++){char thumb[256];thumb_cache_path(paths[i],thumb);
   if(!unlink(paths[i])){unlink(thumb);cJSON_AddItemToArray(deleted,cJSON_CreateString(paths[i]));}
   else cJSON_AddItemToArray(failed,cJSON_CreateString(paths[i]));
  }free(paths);cJSON_AddItemToObject(j,"deleted",deleted);cJSON_AddItemToObject(j,"failed",failed);return j;
 }
 if(!strcmp(op,"thumbnailStart")){
  if(thumb_phase)return fail("A thumbnail is already being prepared");
  if(!valid_media(str(o,"path"),thumb_source))return fail("Media is outside the gallery");
  thumb_cache_path(thumb_source,thumb_dest);cJSON*j=ok();
  if(complete_jpeg(thumb_dest)){utimensat(AT_FDCWD,thumb_dest,NULL,0);cJSON_AddStringToObject(j,"path",thumb_dest);return j;}
  thumb_prune();thumb_cancel=0;thumb_phase=1;
  if(pthread_create(&thumb_thread,NULL,thumbnail_main,NULL)){thumb_phase=0;cJSON_Delete(j);return fail("Cannot start thumbnail worker");}
  cJSON_AddBoolToObject(j,"pending",1);return j;
 }
 if(!strcmp(op,"thumbnailResult")){
  pthread_mutex_lock(&lock);int phase=thumb_phase;cJSON*j=thumb_result;if(phase==2){thumb_result=NULL;thumb_phase=0;}pthread_mutex_unlock(&lock);
  if(phase==2){pthread_join(thumb_thread,NULL);return j;}
  if(!phase)return fail("No pending thumbnail");j=ok();cJSON_AddBoolToObject(j,"pending",1);return j;
 }
 if(!strcmp(op,"play")){
  if(camera_started||avi||photo_phase)return fail("Release camera before playback");
  char real[4096],uri[12300],source[12400],sink[700];if(!valid_media(str(o,"path"),real)||media_kind(real)!=2)return fail("Select a gallery video");
  stop_playback();playback_cleanup();playback_generation++;playback_status=0;playback_presented=0;playback_last[0]=playback_image[0]=0;
  snprintf(playback_prefix,sizeof(playback_prefix),"play-%u-",playback_generation);media_uri(real,uri,sizeof(uri));snprintf(source,sizeof(source),"uri=%s",uri);
  snprintf(sink,sizeof(sink),"video-sink=videoconvert ! videoscale ! video/x-raw,width=%d,pixel-aspect-ratio=1/1 ! jpegenc quality=76 ! multifilesink location=" CACHE "/%s%%08d.jpg max-files=4 sync=true",!strcmp(str(o,"orientation"),"portrait")?264:640,playback_prefix);
  char *argv[]={"gst-launch-1.0","-q","playbin",source,sink,"audio-sink=autoaudiosink",NULL};
  if(!media_spawn(argv,&playback_pid)){playback_pid=0;return fail("Cannot start video decoder");}
  return ok();
 }
 if(!strcmp(op,"playFrame")){
  int ended=0;if(playback_pid&&waitpid(playback_pid,&playback_status,WNOHANG)==playback_pid){playback_pid=0;ended=1;}
  cJSON*j=ok();cJSON_AddBoolToObject(j,"playing",playback_pid!=0);
  if(ended&&(!WIFEXITED(playback_status)||WEXITSTATUS(playback_status)))cJSON_AddStringToObject(j,"error","Video decoder could not play this file");
  DIR*d=opendir(CACHE);char latest[256]={0};if(d){struct dirent*e;while((e=readdir(d)))if(!strncmp(e->d_name,playback_prefix,strlen(playback_prefix))&&strcmp(e->d_name,latest)>0){char path[512];snprintf(path,sizeof(path),CACHE "/%s",e->d_name);if(complete_jpeg(path))snprintf(latest,sizeof(latest),"%s",e->d_name);}closedir(d);}
  if(latest[0]&&strcmp(latest,playback_last)){
   char path[512],presented[256];snprintf(path,sizeof(path),CACHE "/%s",latest);
   if(playback_present(path,presented)){snprintf(playback_last,sizeof(playback_last),"%s",latest);snprintf(playback_image,sizeof(playback_image),"%s",presented);}
  }
  if(playback_image[0])cJSON_AddStringToObject(j,"image",playback_image);return j;
 }
 if(!strcmp(op,"stopPlay")){stop_playback();return ok();}
 return NULL;
}
