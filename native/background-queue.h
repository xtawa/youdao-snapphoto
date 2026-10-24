/* App-owned durable jobs. A bounded helper exits when the queue is empty;
 * no boot service, framework patch, mixer write or permanent daemon. */
#include <sys/file.h>
#include <sys/resource.h>
#define JOBS STATE "/jobs"
static int queue_foreground_fd=-1;
static unsigned queue_serial;
static int queue_name(const char *name){
 size_t n=strlen(name);if(n<12||n>90||strncmp(name,"job-",4)||strcmp(name+n-5,".json"))return 0;
 for(size_t i=4;i<n-5;i++)if((name[i]<'0'||name[i]>'9')&&name[i]!='-')return 0;return 1;
}
static int queue_lock(void){mkdir(STATE,0700);mkdir(JOBS,0700);int fd=open(JOBS "/queue.lock",O_RDWR|O_CREAT|O_CLOEXEC,0600);if(fd>=0&&flock(fd,LOCK_EX)){close(fd);fd=-1;}return fd;}
static cJSON *queue_read(const char *name){
 if(!queue_name(name))return NULL;char path[512];snprintf(path,sizeof(path),JOBS "/%s",name);
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return NULL;struct stat st;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<2||st.st_size>16384){close(fd);return NULL;}
 char text[16385];ssize_t n=read(fd,text,sizeof(text)-1);close(fd);if(n!=st.st_size)return NULL;text[n]=0;return cJSON_Parse(text);
}
static int queue_write(const char *name,cJSON *job){
 if(!queue_name(name))return 0;char path[512],tmp[540];snprintf(path,sizeof(path),JOBS "/%s",name);snprintf(tmp,sizeof(tmp),"%s.%d.tmp",path,getpid());
 char *text=cJSON_PrintUnformatted(job);if(!text)return 0;
 int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);size_t length=strlen(text);int good=fd>=0&&write(fd,text,length)==(ssize_t)length;
 if(fd>=0){if(fsync(fd))good=0;if(close(fd))good=0;}free(text);
 if(good)good=rename(tmp,path)==0;if(!good)unlink(tmp);
 if(good){int dir=open(JOBS,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dir>=0){fsync(dir);close(dir);}}return good;
}
static void queue_value(cJSON *job,const char *key,cJSON *value){cJSON_DeleteItemFromObject(job,key);cJSON_AddItemToObject(job,key,value);}
static void queue_state(cJSON *job,const char *state){queue_value(job,"state",cJSON_CreateString(state));}
static void queue_foreground(int active){
 if(active&&queue_foreground_fd<0){mkdir(STATE,0700);mkdir(JOBS,0700);queue_foreground_fd=open(JOBS "/foreground.lock",O_RDWR|O_CREAT|O_CLOEXEC,0600);if(queue_foreground_fd>=0&&flock(queue_foreground_fd,LOCK_EX)){close(queue_foreground_fd);queue_foreground_fd=-1;}}
 if(!active&&queue_foreground_fd>=0){close(queue_foreground_fd);queue_foreground_fd=-1;}
}
static int queue_has_foreground(void){int fd=open(JOBS "/foreground.lock",O_RDWR|O_CREAT|O_CLOEXEC,0600);if(fd<0)return 0;int active=flock(fd,LOCK_EX|LOCK_NB)<0&&errno==EWOULDBLOCK;close(fd);return active;}
static int queue_worker_path(char path[4096]){
#ifdef SNAP_CLI
 const char *override=getenv("SNAPPHOTO_WORKER");if(override&&strlen(override)<4096){snprintf(path,4096,"%s",override);return 1;}
#endif
 if(!bundled_npu_model())return 0;char base[4096];snprintf(base,sizeof(base),"%s",npu_model);
 char *end=strrchr(base,'/');if(!end)return 0;*end=0;end=strrchr(base,'/');if(!end||strcmp(end,"/models"))return 0;*end=0;
 return snprintf(path,4096,"%s/bin/snapphoto-worker",base)<4096;
}
static int queue_launch(void){
 char program[4096];if(!queue_worker_path(program))return 0;
 struct stat st;if(stat(program,&st)||!S_ISREG(st.st_mode)||chmod(program,0700))return 0;
 /* Double-fork, then exec immediately: the UI may unload its module while
  * the helper runs. Only async-signal-safe calls occur after fork. */
 int log=open(JOBS "/worker.log",O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600),input=open("/dev/null",O_RDONLY|O_CLOEXEC);
 if(log<0||input<0){if(log>=0)close(log);if(input>=0)close(input);return 0;}
 char *args[]={program,"--queue-worker",npu_model,NULL};pid_t first=fork();
 if(first==0){
  if(setsid()<0)_exit(125);pid_t child=fork();if(child<0)_exit(126);if(child>0)_exit(0);
  dup2(input,0);dup2(log,1);dup2(log,2);for(int fd=3;fd<1024;fd++)close(fd);
  execve(program,args,environ);_exit(127);
 }
 close(log);close(input);if(first<0)return 0;int status;while(waitpid(first,&status,0)<0)if(errno!=EINTR)return 0;
 return WIFEXITED(status)&&WEXITSTATUS(status)==0;
}
static cJSON *queue_status(void){
 int fd=queue_lock();if(fd<0)return fail("Cannot read processing queue");
 cJSON *result=ok(),*jobs=cJSON_CreateArray();int pending=0,running=0,failed=0;struct dirent **entries=NULL;
 int count=scandir(JOBS,&entries,NULL,alphasort);
 for(int i=0;i<count;i++){
  cJSON *job=queue_read(entries[i]->d_name);if(job){
   const char *state=str(job,"state");pending+=!strcmp(state,"pending");running+=!strcmp(state,"processing");failed+=!strcmp(state,"error");
   cJSON_AddStringToObject(job,"id",entries[i]->d_name);cJSON_AddItemToArray(jobs,job);
  }free(entries[i]);
 }free(entries);close(fd);cJSON_AddItemToObject(result,"jobs",jobs);
 int owner=open(JOBS "/worker.lock",O_RDWR|O_CREAT|O_CLOEXEC,0600);int alive=owner>=0&&flock(owner,LOCK_EX|LOCK_NB)<0&&errno==EWOULDBLOCK;if(owner>=0)close(owner);
 cJSON_AddBoolToObject(result,"workerAlive",alive);cJSON_AddNumberToObject(result,"pending",pending);cJSON_AddNumberToObject(result,"running",running);cJSON_AddNumberToObject(result,"failed",failed);return result;
}
static cJSON *queue_command(cJSON *o,const char *op){
 if(!strcmp(op,"queueStatus"))return queue_status();
 if(!strcmp(op,"queueResume")){int good=queue_launch();cJSON *r=ok();cJSON_AddBoolToObject(r,"started",good);return r;}
 if(!strcmp(op,"queueAck")){
  int fd=queue_lock();if(fd<0)return fail("Cannot update processing receipt");cJSON *job=queue_read(str(o,"id"));int good=0;
  if(job&&!strcmp(str(job,"state"),"done")){queue_value(job,"published",cJSON_CreateBool(1));good=queue_write(str(o,"id"),job);}cJSON_Delete(job);close(fd);return good?ok():fail("No completed processing job");
 }
 if(strcmp(op,"queueEnhance"))return NULL;
 const char *mode=str(o,"mode");if(strcmp(mode,"auto")&&strcmp(mode,"quality"))return fail("Invalid enhancement mode");
 char source[4096];struct stat st;if(!valid_media(str(o,"path"),source)||strcmp(source,str(o,"path"))||stat(source,&st))return fail("Only canonical gallery JPEGs may be queued");
 const char *ext=strrchr(source,'.');if(!ext||(strcasecmp(ext,".jpg")&&strcasecmp(ext,".jpeg")))return fail("Enhancement requires JPEG input");
 if(!npu_check())return fail("Bundled NPU model unavailable");
 int fd=queue_lock();if(fd<0)return fail("Cannot save processing queue");
 DIR *dir=opendir(JOBS);int pending=0,history=0;char oldest[100]={0};struct dirent *entry;
 while(dir&&(entry=readdir(dir))){cJSON *job=queue_read(entry->d_name);if(!job)continue;
  const char *state=str(job,"state");if(!strcmp(state,"pending")||!strcmp(state,"processing"))pending++;
  else{history++;if(!oldest[0]||strcmp(entry->d_name,oldest)<0)snprintf(oldest,sizeof(oldest),"%s",entry->d_name);}cJSON_Delete(job);
 }if(dir)closedir(dir);
 if(pending>=16){close(fd);return fail("Processing queue is full; original saved");}
 if(history>=64&&oldest[0]){char path[512];snprintf(path,sizeof(path),JOBS "/%s",oldest);unlink(path);}
 cJSON *job=ok();cJSON_AddStringToObject(job,"sourcePath",source);cJSON_AddStringToObject(job,"mode",mode);queue_state(job,"pending");
 cJSON_AddNumberToObject(job,"sourceSize",st.st_size);cJSON_AddNumberToObject(job,"sourceMtime",st.st_mtime);cJSON_AddNumberToObject(job,"sourceInode",st.st_ino);
 double stamp=cJSON_GetNumberValue(cJSON_GetObjectItem(o,"capturedAtEpochMs"));if(!isfinite(stamp)||stamp<946684800000.0||stamp>4102444800000.0)stamp=st.st_mtime*1000.0;
 cJSON_AddNumberToObject(job,"capturedAtEpochMs",stamp);cJSON_AddBoolToObject(job,"keepAlive",!cJSON_IsFalse(cJSON_GetObjectItem(o,"keepAlive")));
 struct timespec now;clock_gettime(CLOCK_REALTIME,&now);char name[100];snprintf(name,sizeof(name),"job-%012ld-%09ld-%d-%u.json",now.tv_sec,now.tv_nsec,getpid(),++queue_serial);
 int good=queue_write(name,job);cJSON_Delete(job);close(fd);if(!good)return fail("Cannot persist enhancement task; original saved");
 cJSON *r=ok();cJSON_AddBoolToObject(r,"queued",1);cJSON_AddStringToObject(r,"jobId",name);cJSON_AddBoolToObject(r,"workerLaunched",queue_launch());return r;
}

#ifdef SNAP_CLI
static volatile sig_atomic_t queue_terminate;
static cJSON *queue_current;static char queue_current_name[100];static double queue_last_report;
static void queue_signal(int signal_number){(void)signal_number;queue_terminate=1;}
static void queue_progress(int progress){
 if(queue_terminate||(!cJSON_IsTrue(cJSON_GetObjectItem(queue_current,"keepAlive"))&&!queue_has_foreground())){pthread_mutex_lock(&enhance_lock);enhance_cancel=1;pthread_mutex_unlock(&enhance_lock);}
 if(enhance_clock()-queue_last_report<500)return;queue_last_report=enhance_clock();
 int fd=queue_lock();if(fd>=0){queue_value(queue_current,"progress",cJSON_CreateNumber(progress));queue_write(queue_current_name,queue_current);close(fd);}
}
static int queue_worker_main(const char *model){
 mkdir(STATE,0700);mkdir(JOBS,0700);int owner=open(JOBS "/worker.lock",O_RDWR|O_CREAT|O_CLOEXEC,0600);
 if(owner<0)return 1;if(flock(owner,LOCK_EX|LOCK_NB)){close(owner);return 0;}
 if(!model||strlen(model)>=sizeof(npu_model)){close(owner);return 2;}snprintf(npu_model,sizeof(npu_model),"%s",model);
 signal(SIGTERM,queue_signal);signal(SIGINT,queue_signal);signal(SIGHUP,SIG_IGN);setpriority(PRIO_PROCESS,0,5);
 char pid[32];int length=snprintf(pid,sizeof(pid),"%d\n",getpid());ftruncate(owner,0);write(owner,pid,length);
 double lifetime=enhance_clock();
 while(!queue_terminate&&enhance_clock()-lifetime<15*60*1000){
  int fd=queue_lock();if(fd<0)break;struct dirent **entries=NULL;int count=scandir(JOBS,&entries,NULL,alphasort);cJSON *job=NULL;char name[100]={0};
  for(int i=0;i<count;i++){
   if(!job){cJSON *candidate=queue_read(entries[i]->d_name);if(candidate){const char *state=str(candidate,"state");
    if((!strcmp(state,"pending")||!strcmp(state,"processing"))&&(cJSON_IsTrue(cJSON_GetObjectItem(candidate,"keepAlive"))||queue_has_foreground())){job=candidate;snprintf(name,sizeof(name),"%s",entries[i]->d_name);}else cJSON_Delete(candidate);
   }}free(entries[i]);
  }free(entries);
  if(!job){close(owner);close(fd);return 0;}
  struct stat st;char real[4096];const char *source=str(job,"sourcePath"),*mode=str(job,"mode");
  int valid=valid_media(source,real)&&!strcmp(source,real)&&!stat(real,&st)&&st.st_size==cJSON_GetNumberValue(cJSON_GetObjectItem(job,"sourceSize"))&&st.st_mtime==cJSON_GetNumberValue(cJSON_GetObjectItem(job,"sourceMtime"))&&st.st_ino==cJSON_GetNumberValue(cJSON_GetObjectItem(job,"sourceInode"))&&(!strcmp(mode,"auto")||!strcmp(mode,"quality"));
  int attempts=(int)cJSON_GetNumberValue(cJSON_GetObjectItem(job,"attempts"));
  if(!valid||attempts>=2){queue_state(job,"error");queue_value(job,"error",cJSON_CreateString(valid?"Interrupted twice; original retained":"Original changed or removed"));queue_write(name,job);cJSON_Delete(job);close(fd);continue;}
  queue_state(job,"processing");queue_value(job,"attempts",cJSON_CreateNumber(attempts+1));queue_value(job,"progress",cJSON_CreateNumber(0));
  queue_write(name,job);close(fd);
  snprintf(enhance_source,sizeof(enhance_source),"%s",source);snprintf(enhance_mode,sizeof(enhance_mode),"%s",mode);
  double stamp=cJSON_GetNumberValue(cJSON_GetObjectItem(job,"capturedAtEpochMs"));if(!isfinite(stamp)||stamp<946684800000.0||stamp>4102444800000.0)stamp=st.st_mtime*1000.0;
  enhance_time.tv_sec=(time_t)(stamp/1000);enhance_time.tv_nsec=(long)(fmod(stamp,1000)*1000000);
  queue_current=job;snprintf(queue_current_name,sizeof(queue_current_name),"%s",name);queue_last_report=0;enhance_cancel=enhance_progress=0;enhance_progress_hook=queue_progress;
  enhance_main(NULL);enhance_progress_hook=NULL;cJSON *receipt=enhance_result;enhance_result=NULL;enhance_phase=0;
  int completed=receipt&&!cJSON_GetObjectItem(receipt,"error");
  int paused=!completed&&(queue_terminate||(!cJSON_IsTrue(cJSON_GetObjectItem(job,"keepAlive"))&&!queue_has_foreground()));
  fd=queue_lock();if(fd>=0){
   queue_state(job,paused?"pending":receipt&&!cJSON_GetObjectItem(receipt,"error")?"done":"error");
   if(paused)queue_value(job,"attempts",cJSON_CreateNumber(attempts));
   else if(receipt){queue_value(job,"result",receipt);receipt=NULL;}
   queue_value(job,"progress",cJSON_CreateNumber(paused?0:100));queue_write(name,job);close(fd);
  }cJSON_Delete(receipt);cJSON_Delete(job);queue_current=NULL;
 }
 snap_npu_close();close(owner);return 0;
}
#endif
