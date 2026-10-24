import {touchPoint} from './swipe.js'

// Two short stationary contacts, anywhere on the screen. Swipes/holds are not taps.
export class DoubleTap {
  constructor(){this.reset()}
  reset(){this.contact=null;this.first=null;this.blockedUntil=0}
  start(event,now){
    this.contact=touchPoint(event)
    if(this.contact)this.contact.time=now
    if(event&&event.touches&&event.touches.length!==1)this.contact=null
  }
  end(event,now){
    const start=this.contact,end=touchPoint(event);this.contact=null
    if(!start||!end||now-start.time>300||Math.hypot(end.x-start.x,end.y-start.y)>24){this.first=null;return false}
    if(now<this.blockedUntil)return false
    if(this.first!==null&&now-this.first<=450){this.first=null;this.blockedUntil=now+900;return true}
    this.first=now;return false
  }
}

const opposite=(a,b)=>(a===0&&b===1)||(a===1&&b===0)||(a===2&&b===3)||(a===3&&b===2)
const faceOpposite=(a,b)=>(a===4&&b===5)||(a===5&&b===4)
export class FlipTracker {
  constructor(){this.reset()}
  reset(){this.baseline=null;this.candidate=null;this.since=0;this.flipped=false}
  sample(value,now){
    if(!value||value.available!==true)return null
    const x=Number(value.x),z=Number(value.z)
    if(!Number.isInteger(x)||x<0||x>5||!Number.isInteger(z)||z<0||z>5)return null
    const key=x+':'+z
    if(this.candidate!==key){this.candidate=key;this.since=now;return null}
    if(now-this.since<600)return null
    if(!this.baseline){this.baseline={x,z};return null}
    const flipped=opposite(this.baseline.x,x)||faceOpposite(this.baseline.z,z)
    // Returning hands rarely reproduce both initial axis classifications exactly.
    // Exit once the stable posture is no longer opposite to the starting posture.
    const restored=!flipped
    if(flipped&&!this.flipped){this.flipped=true;return true}
    if(restored&&this.flipped){this.flipped=false;return false}
    return null
  }
}
