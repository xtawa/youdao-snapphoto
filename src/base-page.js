export class BasePage extends $falcon.Page {
  onShow() {
    super.onShow()
    if (this.$root && this.$root.onShow) this.$root.onShow()
  }
  onHide() {
    if (this.$root && this.$root.onHide) this.$root.onHide()
    super.onHide()
  }
  onUnload() {
    if (this.$root && this.$root.onUnload) this.$root.onUnload()
    super.onUnload()
  }
}
