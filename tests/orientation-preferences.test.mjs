import test from 'node:test'
import assert from 'node:assert/strict'
import {detectOrientation,effectiveOrientation} from '../src/utils/orientation.js'
import {normalizePreferences,loadPreferences,savePreferences} from '../src/utils/preferences.js'
test('portrait, landscape and override',()=>{
  assert.equal(detectOrientation({innerWidth:300,innerHeight:900}),'portrait')
  assert.equal(detectOrientation({screen:{width:936,height:280}}),'landscape')
  assert.equal(effectiveOrientation('portrait','landscape'),'portrait')
  assert.equal(effectiveOrientation('auto','portrait'),'portrait')
})
test('invalid preferences fall back',()=>{
  assert.deepEqual(normalizePreferences({timerDelay:99,burstCount:12,layoutMode:'sideways',showGrid:'yes',photoProfile:'nope'}),
    {timerDelay:0,burstCount:1,layoutMode:'auto',showGrid:false,autoSelfie:true,photoProfile:'wide',videoProfile:'hd',nightMode:'auto',nightPriority:'speed',hdrMode:'off',denoiseMode:'off',processingKeepAlive:true})
})
test('round trips through optional storage',async()=>{
  let stored
  const root={$falcon:{jsapi:{storage:{setStorage:({data})=>{stored=data},getStorage:()=>({data:stored})}}}}
  assert.equal(await savePreferences({timerDelay:10,burstCount:5,showGrid:true,layoutMode:'portrait',photoProfile:'cinema',hdrMode:'auto',denoiseMode:'quality',processingKeepAlive:true},root),true)
  assert.deepEqual(await loadPreferences(root),{timerDelay:10,burstCount:5,showGrid:true,layoutMode:'portrait',autoSelfie:true,photoProfile:'cinema',videoProfile:'hd',nightMode:'auto',nightPriority:'speed',hdrMode:'auto',denoiseMode:'quality',processingKeepAlive:true})
  assert.equal(await savePreferences({},{}),false)
})
