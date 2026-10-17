import { BasePage } from './base-page.js'

class App extends $falcon.App {
  onLaunch(options) {
    super.onLaunch(options)
    $falcon.useDefaultBasePageClass(BasePage)
  }
}
export default App
