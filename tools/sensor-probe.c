/* Read-only: does not grab events, change ranges or enable/disable sensors. */
#include <linux/input.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
int main(int argc,char**argv){
 if(argc>1){int fd=open("/dev/input/event7",O_RDONLY|O_NONBLOCK);if(fd<0)return 1;int last[3]={-99,-99,-99};for(int k=0;k<6000;k++){int values[3];for(int j=0;j<3;j++){struct input_absinfo a={0};ioctl(fd,EVIOCGABS(ABS_RX+j),&a);values[j]=a.value;}if(values[0]!=last[0]||values[1]!=last[1]||values[2]!=last[2]){printf("%d %d %d %d\n",k,values[0],values[1],values[2]);fflush(stdout);for(int j=0;j<3;j++)last[j]=values[j];}usleep(100000);}close(fd);return 0;}
 for(int n=6;n<=7;n++){char p[64],name[128]={0};snprintf(p,sizeof(p),"/dev/input/event%d",n);int fd=open(p,O_RDONLY|O_NONBLOCK);if(fd<0)continue;ioctl(fd,EVIOCGNAME(sizeof(name)),name);printf("%s %s\n",p,name);
 for(int code=0;code<=5;code++){struct input_absinfo a={0};if(!ioctl(fd,EVIOCGABS(code),&a))printf("ABS %d value=%d min=%d max=%d resolution=%d\n",code,a.value,a.minimum,a.maximum,a.resolution);}close(fd);}return 0;
}
