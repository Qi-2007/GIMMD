# GUI 控制台与相机接口进展（2026-10-13）

新增本地 Tk GUI，可创建独立播放会话、连接活动会话、播放/暂停、按秒或拖动进度跳转、切换循环、停止并恢复，以及保存/打开工程。所有引擎访问仍留在已验证的运行时 DLL；界面通过文件协议发送命令，不在 UI 线程调用游戏。

## 使用

双击仓库根目录 `start-studio.cmd`，或执行：

```powershell
.\scripts\start-gui.ps1
# 本机预填的工程，包含已获授权的舞蹈与自制相机示例
.\scripts\start-gui.ps1 -Project .\projects\studio.gimmd.json
```

当前自动定位本机 Codex 随附的 Python；也可用 `-PythonPath` 指定带 tkinter 的 Python 3.10+ `pythonw.exe`。没有下载或安装依赖。

默认构建目录为 `build-studio`，使用 `build-manifest.json` 核对 DLL/加载器 SHA256，且要求完整播放会话构建。选好动作 VMD、可选 PCM WAV 与游戏 PID 后，点击“开始新会话”。每次创建 `sessions/<时间-随机标识>`，复制构建和素材，使用新的 DLL 路径加载；不会覆盖已加载 DLL 或原素材。工程与会话目录已加入 Git 忽略。

```powershell
.\scripts\build.ps1 -RuntimeProbe -SingleTrackPlayback -AnimatorLease -MultiTrackPlayback -FullBodyFK -RootMotion -LegIK -ToeIK -PlayerSession -Test -OutputDirectory build-studio-next
```

当前仍支持已验证的国服 7.1.0 男主模型。没有改成通用角色适配，也没有改变每次会话 90 秒上限。

## 状态与命令

状态协议升级为独立的 schema 2，保留原有字段，增加会话 token、时长、循环标记与 `closed`。状态先写临时文件再替换，避免 GUI 读到半份 JSON。结束标记表示命令传输已结束；姿态是否恢复仍以实机报告为准。

命令仍沿用单条文件邮箱。GUI 按确认序号串行发送，合并队列末尾连续跳转；停止可越过尚未确认的普通命令。重新连接会读取邮箱已有序号，避免重复编号。未知协议、无效数值、过期或已结束状态会停用控制；切换 token 清除旧队列。启动期间关闭界面会等待会话可控后发出停止，正常关闭等待结束确认。

12 项 Python 测试通过，包含真实 Tk 按钮、布局尺寸、序号/确认、停止优先、跳转边界、状态过期、重连、启动期关闭、独立暂存和构建篡改拒绝。C++ 全身版本通过 696 项核心检查、156 项运行时检查。

自动实机验证直接使用 GUI 的同一 Controller/暂存/加载代码：播放 → 暂停（250 ms 保持时间不变）→ 跳至 1.5 秒 → 开启循环 → 继续 → 停止。5 条命令全部确认，结束状态 `closed=true`，52 个旋转目标、根位置及 Animator 恢复，Hook 与音频资源已清理。这次验证确认循环开关生效，没有重复等待整段循环回绕。

[控制链事件](evidence/studio-live-validation.json)、[实机报告](evidence/genshin-7.1-studio-control.json)。

## 相机轨道预览

GUI 的相机页可读取独立相机 VMD，按指定秒数显示转换后的位置、旋转、FOV 与投影模式。`gimmd-camera.exe` 使用已有 VMD 插值及相邻帧切镜语义，按 EIEM 的负 Euler、Y*X*Z 顺序和 Y 轴 180° 源基转换，并使用参考腿长单位比例。CLI 默认零位置/单位旋转的参考锚点，输出并非当前游戏世界坐标。

```powershell
python .\scripts\generate-camera-fixture.py .\build-studio-next\camera-fixture.vmd
.\build-studio-next\gimmd-camera.exe .\build-studio-next\camera-fixture.vmd 0.5
```

自制 3 关键帧、2 秒示例在 0.5 秒输出 FOV 52.5°，供坐标与插值核对。GUI 明确标为离线预览，尚未把相机轨道持续写入客户端；正交状态仅预览，FOV 写入也尚未启用。

## 相机身份与可逆写入

只读采样验证了相机名称、名称别名、所属对象/Transform 一致性，并比较当前 FOV 回调对象的 managed/native 身份：

| 相机 | `Camera.main` | 匹配 FOV 回调 | 位置 |
|---|---|---|---|
| UICamera | 是 | 否 | `[0, 0, -2]` |
| CaptureSkyBoxCamera | 否 | 否 | 与世界相机相同 |
| MainCamera(Clone) | 否 | 是 | 世界相机坐标 |

四次采样一致。这解决了误用 `Camera.main` 或仅靠相同坐标选错对象的问题，但不等同于已经验证最终渲染所有权或持续写入时序。

离线逐层核对世界位置/旋转包装器和原生存储路径后，新增默认关闭的独立实验：

```powershell
.\scripts\build.ps1 -RuntimeProbe -CameraRoundTrip -Test -OutputDirectory build-camera-next
```

仅在名称验证且匹配 FOV 回调的 `MainCamera(Clone)` 上，在同一回调内写入 +0.02 世界 X 位移、0.5° 世界 Y 转角，再分别恢复。全 EXE 指纹与接口入口字节固定；每次调用检查 Transform 原生身份，恢复最多 8 次，无引擎对象指针跨回调保存。禁止与角色姿态写入实验混用。

实机四次 setter 调用、一次恢复尝试，位置/旋转写入和恢复分量误差全部为 0，异常为 0，无待清理状态，Hook 停用且入口字节恢复。无需视觉观察，未播放相机轨道或写入 FOV。相机版本通过 696 项核心检查、40 项运行时检查，新增覆盖重复消费、部分写入异常及身份失联后的有界失败。

[相机实机报告](evidence/genshin-7.1-camera-roundtrip.json)、[包装器](evidence/camera-world-wrappers.json)、[原生路径](evidence/camera-world-native.json)、[存储路径](evidence/camera-world-storage.json)。

下一步是把相机姿态接入会话时间轴，验证游戏相机驱动的覆盖时序和最终恢复，再加入自由相机与 FOV。斜坡地形适配、捩骨、表情仍待实现。
