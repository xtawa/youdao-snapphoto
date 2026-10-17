# X7 Pro 第三方 MiniApp 消失 / 被系统清理排查

没有经 X7 Pro 真机验证的通用防删开关。系统更新或桌面刷新后图标消失，不能直接等同于应用包被卸载。

## 排查顺序

1. 记录系统版本、应用版本、稳定的唯一 appid 与 AMR 文件 SHA-256。
2. 正常安装后验证打开一次，记录是否出现图标。
3. 分别在重启后、Wi-Fi 连接后、固件更新后重复验证，定位触发时间。
4. 区分图标消失、MiniApp 数据库/安装目录被清理、无法启动或包校验失败。
5. 收集最少量设备端输出。优先使用仓库的只读诊断脚本；不要公开设备唯一标识符、日志 token 或私人文件。

## 现在可靠的措施

- 固定独有的 appid；升级版本号时不替换 ID，以免被视为不同应用。
- GitHub Actions 保存可重新安装的 AMR 包及其哈希；另外备份到本机。
- 图片 / 视频放在用户可访问的数据目录，与可重新安装的应用文件分离；具体持久性须真机确认。
- 真正被删后，在用户主动授权 ADB 的情况下手动重新安装；绝不悄悄自安装、伪装系统程序或阻止用户卸载。
- 更新系统前导出影像并保留构建产物；避免在未备份情况下修改系统。

## 只读诊断

在得到设备 ADB 授权后：

    adb push tools/miniapp-retention-diagnose.sh /userdisk/Favorite/miniapp-retention-diagnose.sh
    adb shell "sh /userdisk/Favorite/miniapp-retention-diagnose.sh"

若设备不允许目标目录写入，改用自行确认可写的用户目录。脚本只读取命令可用性、挂载类型和若干目录是否存在。

## 潜在原因（假设，尚未证实）

- 启动器图标缓存或应用数据库重建
- 应用 ID 与已有包冲突
- OTA 替换、数据分区清理或恢复出厂
- 签名 / 包校验失败

不能从此前其他型号的社区帖子推定 X7 Pro 有哪个“白名单”会自动删除自制应用。

## 参考资料

- 社区 MiniApp 安装方式：https://baigei.cc/index.php/archives/566/
- 讨论 X7 Pro OTA 差分更新现象：https://github.com/orgs/PenUniverse/discussions/277
- penosext 提供针对 X7 的 AMR 构建脚本：https://github.com/penosext/miniapp/blob/main/.github/workflows/build_for_x7.yml

不建议替换系统安装器或修改只读分区来“防卸载”；这些操作可能破坏更新、产生权限风险，也不能替代真实原因排查。
