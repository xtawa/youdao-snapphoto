/* Observe the stock RKAIQ continuous AF. Never send motor writes: the ISP
 * service owns the lens. Before still capture, wait briefly for its position
 * to settle; this is not a tap-to-focus or AF-lock API. */
static pthread_t af_thread;
static int af_phase,af_cancel;
static double af_x=.5,af_y=.5,af_score,af_light;
static unsigned long af_sequence;
static cJSON *af_result;
static double af_motion,af_cache_at,af_cache_score,af_cache_light;
static int af_cache_position=-1,af_previous_valid;
static unsigned char af_previous[48*32];
static int af_monitor_fd=-1,af_monitor_attempted,af_motor_position=-1,af_motor_stable,af_guard_enabled;
static double af_preview_history[6],af_preview_at;
static unsigned af_preview_count;
static int lens_open(struct v4l2_queryctrl *q){
 int fd=open("/dev/v4l-subdev8",O_RDONLY|O_CLOEXEC);if(fd<0)return -1;
 char name[100]={0};FILE *f=fopen("/sys/class/video4linux/v4l-subdev8/name","r");if(f){fgets(name,sizeof(name),f);fclose(f);}
 if(!strstr(name,"dw9714")){close(fd);return -1;}
 memset(q,0,sizeof(*q));q->id=V4L2_CID_FOCUS_ABSOLUTE;
 if(xio(fd,VIDIOC_QUERYCTRL,q)<0||(q->flags&V4L2_CTRL_FLAG_DISABLED)){close(fd);return -1;}return fd;
}
static int lens_available(void){struct v4l2_queryctrl q;int fd=lens_open(&q);if(fd<0)return 0;close(fd);return 1;}
static void focus_observe(const unsigned char *src,int pitch){
 pthread_mutex_lock(&lock);double fx=af_x,fy=af_y;int z=zoom,w=photo_width,h=photo_height,rotation=camera_rotation;pthread_mutex_unlock(&lock);
 if(rotation%180){int temp=w;w=h;h=temp;}
 if(!af_monitor_attempted){struct v4l2_queryctrl q;af_monitor_fd=lens_open(&q);af_monitor_attempted=1;}
 struct v4l2_control motor={.id=V4L2_CID_FOCUS_ABSOLUTE};int have_motor=af_monitor_fd>=0&&xio(af_monitor_fd,VIDIOC_G_CTRL,&motor)==0;
 int cw=width*100/z,ch=height*100/z;if(cw*h>ch*w)cw=ch*w/h;else ch=cw*h/w;
 int cx=(width-cw)/2+(int)(fx*cw),cy=(height-ch)/2+(int)(fy*ch);
 int rw=cw/5,rh=ch/4;if(rw<40)rw=40;if(rh<40)rh=40;
 double sum=0,light=0,motion=0;int count=0;
 for(int j=0;j<32;j++)for(int i=0;i<48;i++){
  int x=cx-rw/2+i*rw/48,y=cy-rh/2+j*rh/32;
  if(x<2)x=2;if(x>width-3)x=width-3;if(y<2)y=2;if(y>height-3)y=height-3;
  int at=y*pitch+x,c=src[at],dx=2*c-src[at-2]-src[at+2],dy=2*c-src[at-2*pitch]-src[at+2*pitch];
  sum+=dx*dx+dy*dy;light+=c;
  if(af_previous_valid)motion+=abs(c-af_previous[count]);af_previous[count]=c;count++;
 }
 double mean=light/count;
 motion=af_previous_valid?motion/count/fmax(mean,16):1;af_previous_valid=1;
 pthread_mutex_lock(&lock);af_score=sum/count/((mean+8)*(mean+8))*1000;af_light=mean;af_motion=motion;af_sequence=raw_sequence;
 double time=now_ms();
 if(!have_motor||motor.value!=af_motor_position||time-af_preview_at>150){af_motor_stable=0;af_preview_count=0;}
 else af_motor_stable++;
 if(have_motor)af_guard_enabled=1;
 af_motor_position=have_motor?motor.value:-1;af_preview_at=time;
 af_preview_history[af_preview_count++%6]=af_score;
 if(have_motor&&af_motor_stable>=5&&af_light>=4&&focus_scores_stable(af_preview_history,af_preview_count<6?af_preview_count:6,af_motion)){
  af_cache_at=time;af_cache_position=motor.value;af_cache_score=af_score;af_cache_light=af_light;
 }else af_cache_at=0;
 pthread_cond_broadcast(&frame_cond);pthread_mutex_unlock(&lock);
}
/* Called by the camera thread only, after its last observation. */
static void focus_preview_end(void){
 if(af_monitor_fd>=0)close(af_monitor_fd);af_monitor_fd=-1;af_monitor_attempted=0;af_previous_valid=0;
 pthread_mutex_lock(&lock);af_motor_position=af_cache_position=-1;af_guard_enabled=af_motor_stable=0;af_preview_count=0;af_cache_at=0;pthread_mutex_unlock(&lock);
}
static void *focus_main(void *unused){
 (void)unused;struct v4l2_queryctrl q;int fd=lens_open(&q),previous=-1,stable=0,settled=0;double began=now_ms();
 unsigned long sequence=0;cJSON *samples=cJSON_CreateArray();double score=0,light=0,history[4]={0};int history_count=0;
 while(fd>=0&&now_ms()-began<1400){
  pthread_mutex_lock(&lock);struct timespec limit;clock_gettime(CLOCK_REALTIME,&limit);limit.tv_nsec+=150000000;
  if(limit.tv_nsec>=1000000000){limit.tv_nsec-=1000000000;limit.tv_sec++;}
  while(af_sequence<=sequence&&!af_cancel&&!quit_camera)if(pthread_cond_timedwait(&frame_cond,&lock,&limit)==ETIMEDOUT)break;
  int cancelled=af_cancel||quit_camera;unsigned long current=af_sequence;score=af_score;light=af_light;double motion=af_motion;pthread_mutex_unlock(&lock);
  if(cancelled)break;if(current<=sequence)continue;sequence=current;
  struct v4l2_control control={.id=V4L2_CID_FOCUS_ABSOLUTE};if(xio(fd,VIDIOC_G_CTRL,&control)<0)break;
  stable=control.value==previous?stable+1:0;previous=control.value;
  history[history_count++%4]=score;
  cJSON *sample=cJSON_CreateObject();cJSON_AddNumberToObject(sample,"position",previous);cJSON_AddNumberToObject(sample,"score",score);cJSON_AddItemToArray(samples,sample);
  if(stable>=3&&now_ms()-began>=140&&light>=4&&focus_scores_stable(history,history_count<4?history_count:4,motion)){settled=1;break;}
 }
 if(fd>=0)close(fd);cJSON *result=ok();cJSON_AddBoolToObject(result,"settled",settled);cJSON_AddNumberToObject(result,"position",previous);
 cJSON_AddNumberToObject(result,"score",score);cJSON_AddNumberToObject(result,"luminance",light);cJSON_AddNumberToObject(result,"durationMs",now_ms()-began);
 cJSON_AddBoolToObject(result,"cached",0);cJSON_AddStringToObject(result,"method","Observe stock AF motor, sharpness stability and motion; no lens writes");cJSON_AddItemToObject(result,"samples",samples);
 pthread_mutex_lock(&lock);if(settled){af_cache_at=now_ms();af_cache_position=previous;af_cache_score=score;af_cache_light=light;}af_result=result;af_phase=2;pthread_mutex_unlock(&lock);return NULL;
}
static cJSON *focus_start(cJSON *o){
 (void)o;struct v4l2_queryctrl q;int fd=lens_open(&q);if(fd<0)return fail("No continuous focus status available");
 struct v4l2_control position={.id=V4L2_CID_FOCUS_ABSOLUTE};int queried=xio(fd,VIDIOC_G_CTRL,&position)==0;close(fd);
 pthread_mutex_lock(&lock);if(!ready||quit_camera||af_phase||photo_phase){pthread_mutex_unlock(&lock);return fail("Camera busy or frame not ready");}
 if(queried&&af_cache_at>0&&focus_cache_usable(now_ms()-af_cache_at,af_score,af_cache_score,af_light,af_cache_light,af_motion,position.value==af_cache_position)){
  af_cache_at=now_ms();
  af_result=ok();cJSON_AddBoolToObject(af_result,"settled",1);cJSON_AddBoolToObject(af_result,"cached",1);cJSON_AddNumberToObject(af_result,"durationMs",0);
  cJSON_AddNumberToObject(af_result,"position",position.value);cJSON_AddNumberToObject(af_result,"score",af_score);cJSON_AddStringToObject(af_result,"method","Recent unchanged AF position and stable scene; no lens writes");
  af_phase=3;pthread_mutex_unlock(&lock);return ok();
 }
 af_x=af_y=.5;af_sequence=0;af_phase=1;af_cancel=0;pthread_mutex_unlock(&lock);
 if(pthread_create(&af_thread,NULL,focus_main,NULL)){pthread_mutex_lock(&lock);af_phase=0;pthread_mutex_unlock(&lock);return fail("Cannot start focus observer");}return ok();
}
static cJSON *focus_result(void){
 pthread_mutex_lock(&lock);int phase=af_phase;cJSON *r=af_result;if(phase==2||phase==3){af_result=NULL;af_phase=0;}pthread_mutex_unlock(&lock);
 if(phase==1){cJSON *j=ok();cJSON_AddBoolToObject(j,"pending",1);return j;}
 if(phase==2||phase==3){if(phase==2)pthread_join(af_thread,NULL);return r?r:fail("No focus result");}return fail("No pending focus observation");
}
static void focus_shutdown(void){
 pthread_mutex_lock(&lock);af_cancel=1;int phase=af_phase;pthread_cond_broadcast(&frame_cond);pthread_mutex_unlock(&lock);
 if(phase==1||phase==2)pthread_join(af_thread,NULL);
 pthread_mutex_lock(&lock);af_phase=0;af_cache_at=0;af_cache_position=-1;cJSON_Delete(af_result);af_result=NULL;pthread_mutex_unlock(&lock);
}
