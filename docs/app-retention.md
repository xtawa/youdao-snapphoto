# X7 Pro / PenOS 4.34.0 防系统自动卸载

已启用原厂桌面的“关闭应用白名单卸载”开关。设备标志文件为 `/userdata/.disable_app_whitelist_clean`，启用前不存在，启用后已读回确认。该文件在 ext4 用户数据分区，未修改系统镜像、应用注册表或第三方安装包。

## 核实依据

- 原厂设置 `debugmodeMixin-756048f0.js.bin`：界面文案“关闭应用白名单卸载”；打开时创建该文件，关闭时删除该文件。
- 当前设备桌面 2.34.0，appid `8080222437664451`，模块 `indexPage-b6434364.js.bin` 已完整静态解析 516 个函数到文件末尾。清理函数 file offset `0x207d6` 在 PC 8 检查文件，存在则在 PC 39 返回；日志为 `disabled by test flag, skip cleanUnofficialApps`。
- 默认清理流程请求远端 `appWhitelist`，配置启用且 allowedApps 非空后，枚举已装应用；跳过 flag 位 1/2 或白名单内 appid，其余调用 `removePackage`。
- 该流程不检查 manifest 的 `supportUnInstall`。因此仅把应用改成不可手动卸载不能解决此处的系统清理。
- 此前日志确认清理器实际运行，但远端配置获取失败；未观察到当时已成功删除应用。

## 使用与恢复

电脑已解锁 ADB 时：

```text
adb -d push miniapp-retention-control.sh /tmp/miniapp-retention-control.sh
adb -d shell sh /tmp/miniapp-retention-control.sh status
adb -d shell sh /tmp/miniapp-retention-control.sh enable
adb -d shell sh /tmp/miniapp-retention-control.sh disable
```

`enable` 关闭系统白名单清理，`disable` 恢复默认清理策略。用户仍可手动卸载应用。当前已执行 enable，无需重复操作。

也可直接恢复默认：

```text
adb -d shell rm -f /userdata/.disable_app_whitelist_clean
```

## 验证范围

已核实当前固件代码、文件读回和挂载状态；本轮没有重启设备，也未主动触发真实删除。后续清理器运行时预期输出上述 skip 日志，这项运行时验证仍待自然触发。该开关只控制当前桌面的白名单清理，不覆盖恢复出厂或未来固件改变机制后的删除。
