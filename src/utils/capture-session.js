export const TIMER_CHOICES = [0,3,5,10]
export const BURST_CHOICES = [1,3,5]
export function validCaptureOptions(input) {
  const delay=Number(input && input.delay), count=Number(input && input.count)
  if(TIMER_CHOICES.indexOf(delay)<0 || BURST_CHOICES.indexOf(count)<0) throw new RangeError('无效的倒计时或连拍张数')
  return {delay,count}
}
export class CaptureSession {
  constructor(clock) {
    this.clock=clock || {setTimeout,clearTimeout}
    this.generation=0;this.running=false;this.pending=null
  }
  wait(ms) {
    return new Promise(resolve=>{
      const task={id:null,resolve}
      task.id=this.clock.setTimeout(()=>{if(this.pending===task)this.pending=null;resolve(true)},ms)
      this.pending=task
    })
  }
  cancel() {
    this.generation++
    if(this.pending){const task=this.pending;this.pending=null;this.clock.clearTimeout(task.id);task.resolve(false)}
  }
  async run(input) {
    if(this.running)throw new Error('拍摄任务正在执行')
    const {delay,count}=validCaptureOptions(input)
    if(typeof input.capture!=='function')throw new TypeError('缺少拍照回调')
    const generation=++this.generation
    this.running=true
    let completed=0
    try {
      for(let shot=0;shot<count;shot++){
        if(generation!==this.generation)break
        if(shot===0){
          for(let n=delay;n>0;n--){
            if(generation!==this.generation)break
            if(input.onTick)input.onTick(n)
            const ok=await this.wait(1000)
            if(!ok||generation!==this.generation)break
          }
          if(input.onTick)input.onTick(0)
          if(generation!==this.generation)break
        }else{
          const ok=await this.wait(500)
          if(!ok||generation!==this.generation)break
        }
        const result=await input.capture(shot)
        completed++
        if(input.onShot)await input.onShot(result,shot+1,count)
      }
      return {completed,cancelled:generation!==this.generation}
    }finally{
      if(input.onTick)input.onTick(0)
      this.running=false
    }
  }
}
