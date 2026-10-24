/* MJPEG + PCM AVI. The configured ALSA capture plugin owns and restores the
 * microphone routing. No system mixer or camera-service changes. */
typedef struct _snd_pcm snd_pcm_t;
static void *audio_lib;
static int (*pcm_open)(snd_pcm_t**,const char*,int,int);
static int (*pcm_params)(snd_pcm_t*,int,int,unsigned,unsigned,int,unsigned);
static long (*pcm_read)(snd_pcm_t*,void*,unsigned long);
static int (*pcm_recover)(snd_pcm_t*,int,int),(*pcm_drop)(snd_pcm_t*),(*pcm_close)(snd_pcm_t*);
static int (*pcm_wait)(snd_pcm_t*,int),(*pcm_start)(snd_pcm_t*);
static int load_audio(void){
 if(audio_lib)return pcm_open&&pcm_params&&pcm_read&&pcm_recover&&pcm_drop&&pcm_close&&pcm_wait&&pcm_start;
 audio_lib=dlopen("libasound.so.2",RTLD_NOW|RTLD_LOCAL);if(!audio_lib)return 0;
#define ALSA(v,n) *(void**)(&v)=dlsym(audio_lib,n)
 ALSA(pcm_open,"snd_pcm_open");ALSA(pcm_params,"snd_pcm_set_params");ALSA(pcm_read,"snd_pcm_readi");
 ALSA(pcm_recover,"snd_pcm_recover");ALSA(pcm_drop,"snd_pcm_drop");ALSA(pcm_close,"snd_pcm_close");ALSA(pcm_wait,"snd_pcm_wait");ALSA(pcm_start,"snd_pcm_start");
#undef ALSA
 return pcm_open&&pcm_params&&pcm_read&&pcm_recover&&pcm_drop&&pcm_close&&pcm_wait&&pcm_start;
}
static pthread_mutex_t record_lock=PTHREAD_MUTEX_INITIALIZER;
static FILE *avi;
static double record_limit_time;
static int record_limit;static long record_limit_bytes=0x70000000L;
static snd_pcm_t *audio_pcm;
static pthread_t audio_thread;
static int audio_running,audio_quit,audio_xruns,audio_last_error,audio_zero_reads;
static char audio_error[180];
static uint32_t avi_frames,avi_samples;
static double avi_started_ms,avi_last_ms;
static long avi_rate_pos,avi_movi,avi_frames_pos,avi_length_pos,avi_audio_length_pos;
struct avi_index {uint32_t id,flags,offset,size;};
static struct avi_index *avi_index;
static size_t avi_count,avi_capacity;
static void le16(FILE *f,unsigned n){unsigned char b[]={n,n>>8};fwrite(b,1,2,f);}
/* Called with record_lock. Index growth and the RIFF size are bounded. */
static int avi_chunk(uint32_t id,const void *p,uint32_t n,int key){
 if(!avi)return 0;
 if(ftell(avi)+(long)n+(long)(avi_count+1)*16+16>record_limit_bytes||avi_count>=300000){video_stop=1;record_limit=1;record_limit_time=now_ms();return 0;}
 if(id==0x63643030&&avi_frames%30==0){struct statvfs space;if(!statvfs(GALLERY,&space)&&(uint64_t)space.f_bavail*space.f_frsize<32ULL*1024*1024){video_stop=1;record_limit=2;record_limit_time=now_ms();return 0;}}
 if(avi_count==avi_capacity){
  size_t capacity=avi_capacity?avi_capacity*2:2048;
  void *v=realloc(avi_index,capacity*sizeof(*avi_index));if(!v){video_stop=1;return 0;}
  avi_index=v;avi_capacity=capacity;
 }
 struct avi_index item={id,key?16:0,(uint32_t)(ftell(avi)-avi_movi),n};
 le32(avi,id);le32(avi,n);
 if(fwrite(p,1,n,avi)!=n){video_stop=1;return 0;}if(n&1)fputc(0,avi);
 avi_index[avi_count++]=item;return 1;
}
static void avi_frame(const unsigned char *p,unsigned long n){
 pthread_mutex_lock(&record_lock);
 if(avi&&!audio_quit&&!video_stop&&avi_chunk(0x63643030,p,(uint32_t)n,1)){avi_frames++;avi_last_ms=now_ms();}
 pthread_mutex_unlock(&record_lock);
}
static void *audio_main(void *unused){
 (void)unused;int16_t samples[2048],mono[1024];double last_sample=now_ms();
 while(1){
  pthread_mutex_lock(&record_lock);int quit=audio_quit;pthread_mutex_unlock(&record_lock);if(quit)break;
  long n=pcm_read(audio_pcm,samples,1024);
  if(n==-EAGAIN||n==0){
   if(!n)audio_zero_reads++;
   if(now_ms()-last_sample>1200){pthread_mutex_lock(&record_lock);video_stop=1;audio_last_error=-ETIMEDOUT;pthread_mutex_unlock(&record_lock);break;}
   pcm_wait(audio_pcm,50);continue;
  }
  if(n<0){audio_last_error=(int)n;
   pthread_mutex_lock(&record_lock);quit=audio_quit;pthread_mutex_unlock(&record_lock);if(quit)break;
   /* Do not silently insert missing microphone data or claim a healthy video. */
   if(n==-EPIPE)audio_xruns++;
   if(pcm_recover(audio_pcm,(int)n,1)<0||audio_xruns>3){pthread_mutex_lock(&record_lock);video_stop=1;pthread_mutex_unlock(&record_lock);break;}
   continue;
  }
  if(n){last_sample=now_ms();
   /* PDM stereo capture negotiates the working input; mix to mono with a
    * modest +12dB application-only gain and bounded output, no mixer writes. */
   for(long i=0;i<n;i++){int value=((int)samples[i*2]+samples[i*2+1])*2;if(value>30000)value=30000;if(value< -30000)value=-30000;mono[i]=(int16_t)value;}pthread_mutex_lock(&record_lock);if(!audio_quit&&!video_stop&&avi_chunk(0x62773130,mono,(uint32_t)n*2,1))avi_samples+=(uint32_t)n;pthread_mutex_unlock(&record_lock);}
 }
 return NULL;
}
static int start_avi(const char *path){
 audio_error[0]=0;struct statvfs space;if(!statvfs(GALLERY,&space)&&(uint64_t)space.f_bavail*space.f_frsize<64ULL*1024*1024){snprintf(audio_error,sizeof(audio_error),"Storage space low; at least 64 MB required");return 0;}
 if(!load_audio()||pcm_open(&audio_pcm,"softvol_cap",1,1)<0){snprintf(audio_error,sizeof(audio_error),"Microphone unavailable or in use");audio_pcm=NULL;return 0;}
 if(pcm_params(audio_pcm,2,3,2,16000,1,500000)<0){pcm_close(audio_pcm);audio_pcm=NULL;snprintf(audio_error,sizeof(audio_error),"Microphone format unsupported");return 0;}
 FILE *file=fopen(path,"wb");if(!file){pcm_close(audio_pcm);audio_pcm=NULL;return 0;}
 pthread_mutex_lock(&record_lock);avi=file;avi_frames=avi_samples=0;avi_count=0;record_limit_time=0;video_stop=record_limit=audio_quit=audio_xruns=audio_last_error=audio_zero_reads=0;
 four(avi,"RIFF");le32(avi,0);four(avi,"AVI ");four(avi,"LIST");long hdrl=ftell(avi);le32(avi,0);four(avi,"hdrl");
 four(avi,"avih");le32(avi,56);le32(avi,33333);le32(avi,4032000);le32(avi,0);le32(avi,0x110);
 avi_frames_pos=ftell(avi);le32(avi,0);le32(avi,0);le32(avi,2);le32(avi,record_width*record_height*3);le32(avi,record_width);le32(avi,record_height);for(int i=0;i<4;i++)le32(avi,0);
 four(avi,"LIST");long strl=ftell(avi);le32(avi,0);four(avi,"strl");four(avi,"strh");le32(avi,56);four(avi,"vids");four(avi,"MJPG");le32(avi,0);le32(avi,0);le32(avi,0);
 avi_rate_pos=ftell(avi);le32(avi,1);le32(avi,30);le32(avi,0);avi_length_pos=ftell(avi);le32(avi,0);le32(avi,record_width*record_height*3);le32(avi,0xffffffff);le32(avi,0);le32(avi,0);le32(avi,(record_height<<16)|record_width);
 four(avi,"strf");le32(avi,40);le32(avi,40);le32(avi,record_width);le32(avi,record_height);le32(avi,(24<<16)|1);four(avi,"MJPG");le32(avi,record_width*record_height*3);for(int i=0;i<4;i++)le32(avi,0);patch32(avi,strl,ftell(avi)-strl-4);
 four(avi,"LIST");strl=ftell(avi);le32(avi,0);four(avi,"strl");four(avi,"strh");le32(avi,56);four(avi,"auds");le32(avi,0);le32(avi,0);le32(avi,0);le32(avi,0);le32(avi,2);le32(avi,32000);le32(avi,0);
 avi_audio_length_pos=ftell(avi);le32(avi,0);le32(avi,2048);le32(avi,0xffffffff);le32(avi,2);le32(avi,0);le32(avi,0);
 four(avi,"strf");le32(avi,16);le16(avi,1);le16(avi,1);le32(avi,16000);le32(avi,32000);le16(avi,2);le16(avi,16);
 patch32(avi,strl,ftell(avi)-strl-4);patch32(avi,hdrl,ftell(avi)-hdrl-4);four(avi,"LIST");le32(avi,0);avi_movi=ftell(avi);four(avi,"movi");
 avi_last_ms=avi_started_ms=now_ms();pthread_mutex_unlock(&record_lock);
 if(pcm_start(audio_pcm)<0){pthread_mutex_lock(&record_lock);fclose(avi);avi=NULL;pthread_mutex_unlock(&record_lock);pcm_close(audio_pcm);audio_pcm=NULL;unlink(path);snprintf(audio_error,sizeof(audio_error),"Cannot start microphone capture");return 0;}
 if(!record_encoder_start(record_width,record_height)){pcm_drop(audio_pcm);pcm_close(audio_pcm);audio_pcm=NULL;pthread_mutex_lock(&record_lock);fclose(avi);avi=NULL;pthread_mutex_unlock(&record_lock);unlink(path);return 0;}
 if(pthread_create(&audio_thread,NULL,audio_main,NULL)){
  record_encoder_stop();
  pthread_mutex_lock(&record_lock);fclose(avi);avi=NULL;pthread_mutex_unlock(&record_lock);pcm_close(audio_pcm);audio_pcm=NULL;unlink(path);return 0;
 }
 audio_running=1;return 1;
}
static int finish_avi(double *duration){
 pthread_mutex_lock(&record_lock);double stopped_ms=record_limit_time?record_limit_time:now_ms();pthread_mutex_unlock(&record_lock);record_encoder_stop();
 pthread_mutex_lock(&record_lock);audio_quit=1;pthread_mutex_unlock(&record_lock);
 /* Nonblocking PCM read plus bounded wait lets the worker exit by itself. */
 if(audio_running)pthread_join(audio_thread,NULL);audio_running=0;
 if(audio_pcm){pcm_drop(audio_pcm);pcm_close(audio_pcm);audio_pcm=NULL;}
 pthread_mutex_lock(&record_lock);
 if(!avi){pthread_mutex_unlock(&record_lock);return 0;}
 /* Preserve the wall-clock video timeline; do not squeeze video to hide the
  * short final PCM block. Audio duration stays its exact sample count. */
 *duration=stopped_ms-avi_started_ms;if(*duration<1)*duration=1;
 long end=ftell(avi);patch32(avi,32,(uint32_t)(*duration*1000/(avi_frames?avi_frames:1)));
 patch32(avi,avi_rate_pos,(uint32_t)(*duration+.5));patch32(avi,avi_rate_pos+4,avi_frames*1000);
 patch32(avi,avi_movi-4,end-avi_movi);four(avi,"idx1");le32(avi,(uint32_t)avi_count*16);
 for(size_t i=0;i<avi_count;i++){le32(avi,avi_index[i].id);le32(avi,avi_index[i].flags);le32(avi,avi_index[i].offset);le32(avi,avi_index[i].size);}
 patch32(avi,4,ftell(avi)-8);patch32(avi,avi_frames_pos,avi_frames);patch32(avi,avi_length_pos,avi_frames);patch32(avi,avi_audio_length_pos,avi_samples);
 int broken=(video_stop&&!record_limit)||audio_xruns||!avi_frames||!avi_samples;
 if(fflush(avi)||fsync(fileno(avi))||ferror(avi))broken=1;if(fclose(avi))broken=1;
 avi=NULL;free(avi_index);avi_index=NULL;avi_count=avi_capacity=0;pthread_mutex_unlock(&record_lock);return !broken;
}
