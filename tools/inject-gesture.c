/* Test helper for the connected X7 Pro; logical 936x280, calibrated offset 7. */
#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
static void emit(int fd,int type,int code,int value){struct input_event e={0};e.type=type;e.code=code;e.value=value;write(fd,&e,sizeof(e));}
int main(int argc,char**argv){if(argc!=5)return 2;int fd=open("/dev/input/event4",O_WRONLY);if(fd<0)return 1;
 int x1=atoi(argv[1]),y1=atoi(argv[2]),x2=atoi(argv[3]),y2=atoi(argv[4]);struct input_absinfo info={0};ioctl(fd,EVIOCGABS(ABS_MT_POSITION_Y),&info);int ymax=info.maximum?info.maximum:935;
 emit(fd,EV_ABS,ABS_MT_SLOT,0);emit(fd,EV_ABS,ABS_MT_TRACKING_ID,500);emit(fd,EV_ABS,ABS_MT_TOUCH_MAJOR,8);emit(fd,EV_KEY,BTN_TOUCH,1);
 for(int i=0;i<=20;i++){emit(fd,EV_ABS,ABS_MT_POSITION_X,y1+(y2-y1)*i/20+7);emit(fd,EV_ABS,ABS_MT_POSITION_Y,ymax-(x1+(x2-x1)*i/20));emit(fd,EV_SYN,SYN_REPORT,0);usleep(15000);}
 emit(fd,EV_ABS,ABS_MT_TRACKING_ID,-1);emit(fd,EV_KEY,BTN_TOUCH,0);emit(fd,EV_SYN,SYN_REPORT,0);close(fd);return 0;}
