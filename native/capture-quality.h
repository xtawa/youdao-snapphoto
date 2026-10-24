/* Pure policy helpers. No motor writes, historical-frame reuse or extra ISO. */
static int focus_scores_stable(const double *scores,int count,double motion){
 if(count<4||motion>.12)return 0;
 double lo=scores[0],hi=scores[0];for(int i=1;i<count;i++){if(scores[i]<lo)lo=scores[i];if(scores[i]>hi)hi=scores[i];}
 return hi-lo<=fmax(1.0,hi)*.15;
}
static int focus_cache_usable(double age,double score,double old_score,double light,double old_light,double motion,int same_position){
 return same_position&&age>=0&&age<1200&&motion<.06&&light>=4&&fabs(light-old_light)<fmax(4,old_light*.12)
   &&score>=old_score*.80&&score<=fmax(1.0,old_score)*1.25;
}
static double capture_rank(double sharpness,double motion){return sharpness/(1+fmin(motion,1.0)*3);}
static int capture_replace(double best,double sharpness,double motion){return best<0||capture_rank(sharpness,motion)>best*1.01;}
static int capture_frames(int fast,int night_quality,double focus_age,double motion){
 if(fast)return 1;
 return !night_quality&&focus_age>=0&&focus_age<300&&motion<.02?1:3;
}
