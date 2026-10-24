export class BasePage extends $falcon.Page {
  onShow() {
    console.warn('[snapphoto] page show root='+!!this.$root)
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
