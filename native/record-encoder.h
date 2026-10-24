/* Bounded producer/consumer pipeline. Camera samples a separate video crop;
 * this worker JPEG-encodes it while low-resolution UI preview continues. */
#define RECORD_SLOTS 3
struct record_job {struct preview_encoder image;int state,effect;};
static struct record_job record_jobs[RECORD_SLOTS];
static pthread_mutex_t jobs_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t jobs_cond=PTHREAD_COND_INITIALIZER;
static pthread_t record_thread;
static int jobs_active,jobs_accept,jobs_filling,jobs_w,jobs_h,jobs_write,jobs_read;
static unsigned record_dropped;
static double record_encode_ms,record_sample_ms,jobs_next_ms;
static void *record_encoder_main(void *unused){
 (void)unused;
 while(1){
  pthread_mutex_lock(&jobs_lock);
  while(record_jobs[jobs_read].state!=1&&(jobs_accept||jobs_filling))pthread_cond_wait(&jobs_cond,&jobs_lock);
  if(record_jobs[jobs_read].state!=1){pthread_mutex_unlock(&jobs_lock);break;}
  struct record_job *job=&record_jobs[jobs_read];job->state=2;int effect=job->effect;pthread_mutex_unlock(&jobs_lock);
  unsigned char *jpeg=NULL;unsigned long length=0;double began=now_ms();
  int good=sample_compress(&job->image,effect,85,&jpeg,&length);
  if(good)avi_frame(jpeg,length);else{pthread_mutex_lock(&record_lock);video_stop=1;pthread_mutex_unlock(&record_lock);}
  if(jpeg)jpeg_free(jpeg);
  pthread_mutex_lock(&jobs_lock);record_encode_ms=now_ms()-began;job->state=0;jobs_read=(jobs_read+1)%RECORD_SLOTS;pthread_cond_broadcast(&jobs_cond);pthread_mutex_unlock(&jobs_lock);
 }
 return NULL;
}
static int record_encoder_start(int w,int h){
 pthread_mutex_lock(&jobs_lock);
 if(jobs_active){pthread_mutex_unlock(&jobs_lock);return 0;}
 memset(record_jobs,0,sizeof(record_jobs));jobs_w=w;jobs_h=h;jobs_write=jobs_read=jobs_filling=0;
 record_dropped=0;record_encode_ms=record_sample_ms=jobs_next_ms=0;jobs_active=jobs_accept=1;pthread_mutex_unlock(&jobs_lock);
 if(pthread_create(&record_thread,NULL,record_encoder_main,NULL)){
  pthread_mutex_lock(&jobs_lock);jobs_active=jobs_accept=0;pthread_mutex_unlock(&jobs_lock);return 0;
 }
 return 1;
}
static void record_submit(const unsigned char *nv,int pitch,int z,int effect){
 pthread_mutex_lock(&jobs_lock);
 if(!jobs_accept){pthread_mutex_unlock(&jobs_lock);return;}
 double now=now_ms(),period=1000.0/record_target_fps;if(now<jobs_next_ms){pthread_mutex_unlock(&jobs_lock);return;}
 jobs_next_ms=jobs_next_ms?jobs_next_ms+period:now+period;if(jobs_next_ms<now-period)jobs_next_ms=now+period;
 struct record_job *job=&record_jobs[jobs_write];
 if(job->state){record_dropped++;pthread_mutex_unlock(&jobs_lock);return;}
 job->state=3;jobs_filling=1;int w=jobs_w,h=jobs_h;pthread_mutex_unlock(&jobs_lock);
 double began=now_ms();int good=sample_prepare(&job->image,nv,pitch,w,h,z,effect);
 pthread_mutex_lock(&jobs_lock);record_sample_ms=now_ms()-began;
 if(good&&jobs_accept){job->effect=effect;job->state=1;jobs_write=(jobs_write+1)%RECORD_SLOTS;}else job->state=0;
 jobs_filling=0;pthread_cond_broadcast(&jobs_cond);pthread_mutex_unlock(&jobs_lock);
 if(!good){pthread_mutex_lock(&record_lock);video_stop=1;pthread_mutex_unlock(&record_lock);}
}
static void record_encoder_stop(void){
 pthread_mutex_lock(&jobs_lock);int active=jobs_active;jobs_accept=0;pthread_cond_broadcast(&jobs_cond);pthread_mutex_unlock(&jobs_lock);
 if(active)pthread_join(record_thread,NULL);
 pthread_mutex_lock(&jobs_lock);jobs_active=0;
 for(int i=0;i<RECORD_SLOTS;i++){preview_destroy(&record_jobs[i].image);memset(&record_jobs[i],0,sizeof(record_jobs[i]));}
 pthread_mutex_unlock(&jobs_lock);
}
