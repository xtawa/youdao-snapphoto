// PenOS gsensor constants, verified against the stock camera's getters.
export function sensorRotation(value){
  if(!value || value.available!==true)return null
  return [0,180,90,270][value.x] ?? null
}
// UI rotation is in physical display coordinates. Pixels inside that UI must
// rotate in the opposite direction, otherwise portrait applies 90 + 90 = 180.
export function displayRotation(mode,orientation,rotation){
  if(orientation==='portrait')return mode==='auto'&&rotation===270?270:90
  return mode==='auto'&&rotation===180?180:0
}
export function captureRotation(mode,orientation,rotation){
  return (360-displayRotation(mode,orientation,rotation))%360
}
export class OrientationTracker{
  constructor(){this.candidate=null;this.since=0;this.rotation=0}
  sample(value,now){
    const rotation=sensorRotation(value)
    if(rotation===null)return null // Face up/down: retain the last useful posture.
    if(this.candidate!==rotation){this.candidate=rotation;this.since=now;return null}
    if(now-this.since<500 || this.rotation===rotation)return null
    this.rotation=rotation;return rotation
  }
}
