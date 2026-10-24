/* Read-only V4L2 capabilities; no format/crop/control writes. */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
int main(void){
 int fd=open("/dev/video30",O_RDONLY);if(fd<0){perror("camera");return 1;}
 for(unsigned target=V4L2_SEL_TGT_CROP_DEFAULT;target<=V4L2_SEL_TGT_CROP_BOUNDS;target++){
  struct v4l2_selection s={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,.target=target};
  if(!ioctl(fd,VIDIOC_G_SELECTION,&s))printf("selection %u: %d,%d %ux%u\n",target,s.r.left,s.r.top,s.r.width,s.r.height);
 }
 for(int i=0;i<8;i++){
  struct v4l2_frmsizeenum s={.index=i,.pixel_format=V4L2_PIX_FMT_NV12};if(ioctl(fd,VIDIOC_ENUM_FRAMESIZES,&s))break;
  if(s.type==V4L2_FRMSIZE_TYPE_DISCRETE)printf("frame: %ux%u\n",s.discrete.width,s.discrete.height);
  else printf("frame range: %ux%u to %ux%u step %ux%u\n",s.stepwise.min_width,s.stepwise.min_height,s.stepwise.max_width,s.stepwise.max_height,s.stepwise.step_width,s.stepwise.step_height);
 }
 close(fd);return 0;
}
