import { BasePage } from './base-page.js'
import {installX7ProNative,startScanGuard,stopScanGuard,startScreenGuard,stopScreenGuard} from './platform/x7pro-native.js'

class App extends $falcon.App {
  onLaunch(options) {
    console.warn('[snapphoto] launch')
    super.onLaunch(options)
    this.setViewPort(936)
    installX7ProNative()
    $falcon.useDefaultBasePageClass(BasePage)
  }
  onShow(){super.onShow();startScanGuard();startScreenGuard();console.warn('[snapphoto] app show')}
  onHide(){try{stopScanGuard()}finally{try{stopScreenGuard()}finally{super.onHide()}}}
  onDestroy(){try{stopScanGuard()}finally{try{stopScreenGuard()}finally{super.onDestroy()}}}
}
export default App
