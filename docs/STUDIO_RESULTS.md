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

默认构建目录为 `build-studio-camera-fov`，使用 `build-manifest.json` 核对 DLL/加载器 SHA256，且要求完整播放会话构建。选好动作 VMD、可选 PCM WAV 与游戏 PID 后，点击“开始新会话”。每次创建 `sessions/<时间-随机标识>`，复制构建和素材，使用新的 DLL 路径加载；不会覆盖已加载 DLL 或原素材。工程与会话目录已加入 Git 忽略。

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

自制 3 关键帧、2 秒示例在 0.5 秒输出 FOV 52.5°，供坐标与插值核对。离线预览保留；本轮新增相机轨道同步写入，见下文。正交状态仅预览，FOV 写入尚未启用。

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

下一步是验证最终渲染时序，并加入自由相机与 FOV。斜坡地形适配、捩骨、表情仍待实现。


## 同步相机轨道（2026-10-13）

新增默认关闭的 `-CameraPlayback` 构建选项，要求 `-PlayerSession`，与 `-CameraRoundTrip` 互斥。本机可用构建为 `build-camera-player`：

```powershell
.\scripts\build.ps1 -RuntimeProbe -SingleTrackPlayback -AnimatorLease -MultiTrackPlayback -PlayerSession -FullBodyFK -RootMotion -LegIK -ToeIK -CameraPlayback -Test -OutputDirectory build-camera-player
```

重开控制台后，相机页可勾选“在新会话中启用此相机轨道”。工程保存此开关，旧工程默认关闭。准备会话会先验证构建能力、相机解析和投影类型，再复制独立的 `camera.vmd`；不修改原始素材。整条轨道只要包含正交关键帧就拒绝启用，避免静默忽略投影切换。相机时长不延长动作时间轴；相机轨道提前结束时保持末帧。

相机姿态采用首帧对齐当前机位的相对模式：将源首帧旋转映射到捕获的游戏相机旋转，以同一映射变换轨道的位置差。它用于从现有构图开始移动，尚非 EIEM 的角色舞台绝对锚点。FOV 保持游戏行为。

仅在 `MainCamera(Clone)` 名称、所属对象和 Transform 往返一致，并匹配 FOV 回调身份时取得相机；之后每次回调重新查找，并核对相机与 Transform 原生身份。播放与暂停都持续应用同一时间轴样本，跳转/循环共用 epoch。停止、时间到期、角色失败或相机异常都会进入恢复；待恢复相机纳入 Hook 保留与退出状态检查，不会提前报告恢复成功。

自动实机命令链使用自制两秒相机素材与六秒骨骼校准：播放 → 暂停 → 暂停跳转 1.5 秒 → 开启循环 → 继续 → 停止。最终构建写入 231 次，位置/旋转最大分量误差 `5.96e-8`，一次恢复成功，恢复误差 0，无异常、无待清理状态。角色 52 骨骼、根位移、Animator、音频和 Hook 同时恢复。696 项核心检查、164 项运行时检查和 13 项 GUI/会话测试通过。

152/84（首轮）和 148/82（最终构建）的“保留/被游戏改写”采样说明游戏相机控制器仍在运行。本实现会重新施加轨道，数值验证确认的是写后姿态与恢复，**尚未确认最终渲染帧的持有时序、抖动或构图效果**，不将其报告为完整自由相机。后续需要更合适的最终相机更新阶段或驱动租约。

证据：[自动命令链](evidence/camera-playback-live-validation.json)、[完整实机报告](evidence/genshin-7.1-camera-playback.json)。此轮无需用户视觉反馈。


## Windows 启动修复

双击入口改为 `powershell.exe -NoProfile -ExecutionPolicy Bypass`，仅对启动进程设置执行策略，不修改系统配置。PowerShell 启动脚本使用纯 ASCII，避免 Windows PowerShell 5.1 对无 BOM UTF-8 中文按本地代码页解析而报“字符串缺少终止符”。入口失败时保留终端，Python 标准错误/输出保存至 `logs/gui-<随机标识>.stderr.log` 与 `.stdout.log`；启动后 1.5 秒内退出且非零会明确报告日志路径。已在实际桌面确认 `pythonw` 的“GIMMD · 创作控制台”窗口启动成功。日志目录不纳入 Git。


## FOV、手动机位与运镜编排

`build-studio-camera-fov` 是本轮构建，仍使用 `-CameraPlayback` 开关。该构建清单新增 `cameraFov` 和 `manualCamera` 能力。旧窗口需关闭后重新双击 `start-studio.cmd`；新界面默认选择该目录。工程本机预填配置也已更新，不自动播放。

### FOV 实机控制

离线核对 FOV 原生 setter `0x199c8d0` 的限幅与存储分支：目标是原生 Camera，float 位于 `native+0xa58`，写入后标记 `+0xec0`。新增精确 profile 条目覆盖 setter、分支以及完整的存储指令与偏移；只有完整 EXE 指纹和字节验证通过才启用。读取使用有界 `ReadProcessMemory`，写入复用已挂钩函数的原始 trampoline，不直接修改该字段。仅在已验证的主相机身份上调用，并保存初始 FOV；每次写后读回，停止时与位置/旋转一起恢复。

轨道实机 291 次写入，FOV 写入/恢复误差均为 0，位置/旋转最大分量误差 `5.96e-8`、恢复误差 0。暂停跳转至 1.5 秒后，可手动覆盖至 60°，复位后回到轨道 FOV。最终恢复初始 45°，无异常、无待清理状态。

证据：[FOV 原生分支](evidence/camera-fov-wrapper.json)、[命令链](evidence/camera-fov-live-validation.json)、[完整报告](evidence/genshin-7.1-camera-fov.json)。FOV 是原始 VMD 数值，不默认叠加 EIEM 的 +5° 偏置。

### 手动机位

相机页勾选“启用相机控制”和“仅手动机位”可不提供相机 VMD。此模式仍在角色播放会话中运行，尚非独立于动作的自由视角会话；暂停动作后也能调整机位。位移沿捕获初始相机的右/上/前方向，限制 ±100 世界单位；角度为局部 Y*X*Z 偏航/俯仰/滚转，限制 ±180°。FOV=0 采用轨道值，纯手动模式则保留初始值；1–179° 为手动覆盖。复位清除偏移和 FOV 覆盖，停止交还相机。

界面通过独立原子文件 `camera.command` 发送完整参数与递增序号。工作线程解析有限数值，主线程通过非阻塞锁快照读取，验证写后姿态后才发布 `cameraAcknowledgedSequence`。主线程不读取文件；退出包含 FOV 恢复状态。

纯手动实机 292 次写入，位置、转向、60° FOV 覆盖和复位均已确认，最终位置/旋转/FOV 恢复误差为 0。证据：[命令链](evidence/manual-camera-live-validation.json)、[完整报告](evidence/genshin-7.1-manual-camera.json)。仍有游戏相机覆盖采样，不宣称已验证最终渲染阶段或鼠标/WASD 自由机位。

### 运镜关键帧

新增“运镜编排”页，从相机页的手动输入参数添加或替换指定时刻关键帧，可取回、删除、保存/打开独立 JSON 运镜工程，并导出相机 VMD。这里记录的是输入参数，尚不是读取游戏当前相机的实时录制。必须先添加第 0 秒初始机位；时间按 30 fps 对齐，VMD FOV 按整数取整。FOV=0 在编排时使用明确标注的 45°。

导出为原始相机 VMD，线性 Bezier 曲线，按固定源/目标单位和相机符号换算。回放首帧对齐当时的游戏机位；导出会把文件选入工程，不自动注入或播放。GUI 回归用两个关键帧导出后再交由实际 C++ VMD 采样器读取，验证中间位置和 FOV，并验证 JSON 往返、同帧冲突与参数取回/删除。

### 作品导入与音频偏移

新增 `set.ini` 导入，兼容本地参考 `BE_mmd_library.h` 中 `[set]`、`name/motion/camera/music/face/motion2..4/face2..4/audio_offset` 格式，采用 UTF-8。仅接受作品目录内文件名，拒绝重复字段、缺失文件、路径越界及越界链接；不会执行素材文件。当前需要主动作 `motion`，音频只支持 PCM WAV。表情和额外舞者路径保留到工程的 `importedWork.unsupported` 并提示当前不播放，不把导入成功视为客户端已经支持。

音频偏移遵循 `音频位置 = 动作时间 + audio_offset`。准备独立会话时，正值裁去开头的对应 PCM 样本，负值补静音；原音频不变，新的会话 WAV 继续共用已有播放/暂停/跳转/循环时钟。偏移限制 ±600 秒，处理后不得为空或超过 64 MiB。保存工程和会话清单记录偏移。自动检查逐字节验证正/负偏移样本与原文件保持不变；本轮未做额外的主观音画同步评估。

本轮通过 705 项核心检查、168 项运行时模拟检查和 17 项 GUI/素材测试；轨道 FOV 与纯手动模式分别通过一次自动实机命令链。剩余能力见 [功能状态](FEATURE_STATUS.md)。
