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


## Center 转向兼容修复（构建验证，待用户实测）

`人是猫/动作.vmd` 含 239 条骨骼轨道、13805 个骨骼关键帧，时长约 14.033 秒，无重复帧或零四元数。这是一套普通骨骼轨道，拒绝原因是 Center 存在约 19.78° 的 Y 轴转向，旧版 `root_motion::prepare` 拒绝所有根控制旋转，并非双人轨道需要拆分。

`build-studio-center-yaw` 新增 Center 的纯 Y 轴旋转：先与下半身源旋转合成，再重定向到目标骨盆并进入腿部 IK；Groove 的位置先按 Center 旋转后再合并根位移。它不额外改变游戏视觉根旋转，仍由既有 52 骨骼恢复逻辑恢复。Center 俯仰/滚转、全亲或 Groove 的旋转仍明确拒绝，避免没有源模型枢轴标定时假装完整支持。

VMD CLI 输出 `rootControlCompatibility` 和 `centerYawRequired`，构建清单输出 `centerYaw`，GUI 准备会话可提前报告根控制不兼容；会话被拒绝时也显示具体内部原因。GUI 默认构建目录更新为 `build-studio-center-yaw`。通过 707 项核心检查、170 项运行时模拟检查；按用户要求未进行该动作实机测试。

本机测试入口为 `build-studio-center-yaw/test-cat.cmd`，打开忽略目录中的 `projects/cat.gimmd.json`，已选择动作与 WAV，相机先关闭。用户自行点击开始并观察转身和脚部效果。原动作与音频未改动；素材 readme 要求署名 rona，请勿商用，发布作品时保留该署名。


## 空骨骼名、大文件与烘焙 FK 兼容（待用户实测）

`build-studio-vmd-compat` 修复两个实际素材：`say no/sayso.vmd` 包含两条空名骨骼记录，现完整检查帧、有限数值和插值字段后跳过，输出 `ignoredEmptyBoneKeys=2`，其余 63 条骨骼轨道保留。未放宽损坏记录的检查。`人是猫动作/人是猫60.vmd` 大小 20390774 字节，旧运行时的 16 MiB 限制使其被拒绝；运行时现与解析器统一为 64 MiB，保留 500000 骨骼关键帧限制。

第二个素材是 50 轨道烘焙 FK，时长 122.433 秒：缺少下半身/Center，使用“腰”轨道。仅在标准 Center 与下半身均缺失时，将腰旋转映射到骨盆 FK，腰位移代入缺失的 Center 平移通道；不重复施加腰旋转。无脚 IK 轨道时保留源 FK 腿部动画，不要求 IK 轨道存在、不强加腿部 IK。会话硬时限延长至十分钟，加载器等待相应延长，避免 122 秒动作被 90 秒时限截断。

Sayso 的 Center 同时具有俯仰/滚转。现将其完整旋转合入骨盆，再求解腿部 IK；Groove 平移按 Center 旋转合成。缺少源 PMX 枢轴时，Center 俯仰/滚转使用骨盆枢轴近似，报告明确标注 `centerRotationPivot=pelvis-approximation-without-source-pmx`；尚未验证画面，不能宣称精准枢轴匹配。全亲和 Groove 自身旋转仍明确拒绝。

GUI 默认选择本构建，加载器非零退出后仍优先读取可用 probe 报告，显示具体拒绝原因。通过 709 项核心检查、170 项模拟运行时检查；两个实际素材均通过同一运行时 `multi::prepare` 离线准入检查，未启动或操作游戏。预填本机测试入口为 `build-studio-vmd-compat/test-sayso.cmd` 和 `test-cat60.cmd`；打开后用户自行点击开始新会话，相机默认关闭。素材原文件未改动。

证据：[素材兼容离线检查](evidence/vmd-compatibility-validation.json)。


## 并列上下半身继承修正（待用户实测）

`天天天国地獄国配布用/初始右-173cm.vmd` 首帧上下半身均有近 180° 转向。MMD 的上下半身为并列控制，目标 Spine 则继承 Pelvis；原来直接分别写入会重复叠加骨盆转向，造成腰腹扭折。现在在目标坐标中使用 `inverse(当前骨盆FK) * 参考骨盆 * 独立胸部FK` 得到胸部局部姿态，不抵消其应继承的公共 Center 旋转。此换算保留两个目标绑定各自的轴向校正。仅对有标准下半身轨道的映射启用；使用腰轨道的烘焙 FK 不变。

构建 `build-studio-torso-fix` 通过 711 项核心、170 项模拟运行时检查。实际素材首帧的离线世界旋转重建误差由约 179° 降至数值零；未进行实机测试。披风物理未修改：先排除身体继承错误，再判断动态披风是否仍有独立异常。测试入口 `build-studio-torso-fix/test-ten173-right.cmd`。报告新增 `parallelTorsoCompensated`，证据见 [上下半身重建检查](evidence/torso-inheritance-validation.json)。


## 腰部辅助骨跟随（待用户实测）

用户确认胸部继承修正后腰带/腰部仍固定。现有实机只读快照进一步显示：`+PelvisTwist CF A01` 与 `Bip001 Pelvis` 同为 `Bip001` 的子节点，而非骨盆的子节点；Body 的 91 骨蒙皮列表明确包含该辅助骨，Avatar 参考索引为 123。52 个原有 FK 目标不包含它，所以禁用 Animator 后只转骨盆不会更新此处蒙皮。这解释了“骨盆和胸部动作变化，但腰部网格固定”的反馈；上一版胸部修正不足以解决这一遗漏。

`build-studio-pelvis-helper` 补充一条独立辅助骨轨道。绑定核对同级关系、唯一名称、所属活动模型、原生身份和已验证 Avatar 索引。其局部旋转按 `当前目标骨盆 * inverse(参考骨盆) * 参考辅助骨` 计算，入口从实际保存姿态渐入；与骨盆共用父级坐标，不复制不同轴向的骨盆局部四元数。每批通过既有共享路径缓存重新解析；检测外部覆盖，异常/停止时最多八次独立恢复原始局部旋转，失败纳入整体待恢复状态。没有修改辅助骨缩放、位置、游戏实体或披风物理。

报告新增 `multiTrackPreview.pelvisHelper` 的绑定、写入、覆盖和恢复结果。711 项核心、173 项模拟运行时检查通过，新增模拟覆盖同级骨旋转、轴向换算及原始姿态恢复。用户自行通过 `build-studio-pelvis-helper/test-ten173-right.cmd` 测试，未由助手启动游戏；实际画面效果仍待确认。若腰部修正后披风仍有异常，需要独立检查披风/布料驱动。


## 披风静态稳定实验（待用户实测）

用户已确认腰部辅助骨修复有效；最新播放报告显示其写入 1229 次、无覆盖、停止恢复成功。披风仍抖动扭曲。已识别男主披风对应的 28 根 `+Amice` 骨骼，均有 Avatar 参考链接；原生布料可能仍在更新这些骨骼，但尚未定位并禁用具体物理组件。

新增可选“披风静态稳定（新会话）”开关。开启后绑定当前活动模型内的 Amice 骨骼，核对名称、路径、对象身份与参考索引，以参考局部位置/旋转静态跟随身体，从捕获姿态平滑过渡。每批重新解析，记录外部覆盖而不将预期的布料写入误判为主体骨架失效。停止或异常时独立恢复全部捕获位置和旋转，失败纳入整体清理状态；不修改缩放、头发、其他角色或物理组件。此模式用于隔离与改善披风形变，牺牲自然摆动，不能宣称已禁用物理或保证最终渲染时序无抖动。

会话使用 `cloth-static.flag`，构建清单用 `clothStabilization` 声明能力。报告 `clothStabilizer` 包含骨数、写入数、外部覆盖、清理状态和 `physicsDisabled=false`。本机测试入口为 `build-studio-cloth-static/test-cloth-static.cmd`，预填原动作并开启静态模式；用户可取消复选框作对比。本轮只构建和离线模拟，未启动游戏。


### 静态模式启动即结束修复

用户实测静态模式绑定了 28 根披风骨骼，但 `sampleCount=0`、披风写入数为 0，无引擎异常或待恢复状态。原因是新增的披风路径生成器把无父节点的根步骤子序号设为 0，主体骨架路径及共享缓存使用的根哨兵值为 -1，第一次共享缓存校验失败即停止。现根节点保留 -1，仅对有父节点的步骤计算实际子序号。

`build-studio-cloth-static-fix` 通过 711 项核心和 178 项模拟运行时检查。新增回归使用实际披风路径生成器，先填充主体骨架缓存，再验证披风路径兼容；旧逻辑会失败。测试入口为 `build-studio-cloth-static-fix/test-cloth-static.cmd`，本机原静态项目也已切换到修正构建。未启动游戏，披风稳定效果仍待用户确认。


## 2026-10-13 拍摄辅助：辫子、音乐分类与 UID 画面文字

交付构建 `build-studio-shooting`，GUI 默认采用此目录。`test-shooting.cmd` 载入 173 cm 右侧动作工程，并启用披风与辫子静态、实验音乐暂停和 UID 文字替换；不会自动开始会话。

- 男主辫子仅选择 `+HairB CB A` 后脑骨骼链（现有快照中 4 节），与披风独立开关。共用局部位置/旋转参考姿态写入和原始姿态恢复。未关闭原生物理，最终渲染仍可能受物理更新顺序影响。
- BGM 暂停仅调用已加载且 SHA256 固定的 `cri_ware_unity.dll` 导出接口。枚举 ACF 分类，唯一匹配 BGM（优先）或 Music/BackgroundMusic 才暂停；保存并恢复原有暂停状态，不做整进程或设备静音。CRI 分类接口确认不等于当前原神场景音乐覆盖确认；如果当前 BGM 由其他音频系统播放，此试验不会暂停它。界面和报告明确显示不可用/覆盖待确认。
- UID 仅针对 `/BetaWatermarkCanvas(Clone)/Panel/TxtUID` 的 Text 显示值，路径参考 FufuLauncher UnlockerIsland 的 HideUI.cpp。getter/setter/store 固定 EXE 哈希与指令，运行时另核对 Text 方法名、参数数、实际代码地址与对象归属。写后读回，不匹配则停止该功能并恢复。原始文本仅存内存，不写报告；未修改账号 UID。文字至多 64 字，允许空串。
- 新功能均在游戏主线程回调内应用；停止时执行恢复，恢复待确认时保留钩子与 DLL，避免提前卸载。未运行或操作游戏。本轮仅构建与离线模拟验证，真实效果由用户测试。

参考：CRI 官方 [Category API](https://game.criware.jp/manual/native/adx2_en/latest/group__ATOMEXLIB__CATEGORY.html)、[ACF API](https://game.criware.jp/manual/native/adx2_en/latest/group__ATOMEXLIB__ACF.html)；[Fufu UID 路径参考](https://github.com/FufuLauncher/FufuLauncher.UnlockerIsland/blob/2433a01d4fb4f96d340e82d035750e55b6a2c534/FufuLauncher.UnlockerIsland/HideUI/HideUI.cpp)。接口静态反汇编见 `docs/evidence/ui-text-accessors.json`。


### 准备阶段停止修正与音乐识别反馈

用户报告进入角色场景后可恢复正常。会话 `20261013-203708-82e3821f` 的最终报告显示 `incomplete-hierarchy-refused`、姿态 setter=0、UID captured=false/setterCalls=0、BGM `audio-engine-unavailable`/calls=0；本轮并无证据表明 UID setter 卡死。worker 已读 23 条命令，但 acknowledged=0，确认准备阶段未处理 Stop；另一个问题是 sample_count<4 使已结束的绑定拒绝仍等待采样。

`build-studio-shooting-recovery` 的 Stop 在 worker 读取后立即设置原子停止标志和确认序号，退出等待采样循环；主线程 tick 支持绑定前取消。已关闭状态允许直接关闭控制台，不再等待加载器退出。音频插件若在登录后才加载，worker 会间隔检测并仅在模块已加载时校验导出；不会主动加载插件。此修正不证明 CRI 能覆盖场景 BGM，测试工程默认关闭实验音乐暂停。当前可靠替代是用户在游戏音频设置中把音乐音量调为 0，结束后手动恢复。


## 2026-10-13 VMD 运镜偏移与 FOV 倍率

交付构建 `build-studio-camera-adjust`，入口 `test-camera-adjust.cmd`。保留原有机位的右/上/前位移及偏航/俯仰/滚转调整，新增 FOV 倍率（0.1–4，默认 1）。每帧对 VMD 采样完成后叠加位置偏移；位置轴沿会话初始机位，因此不会随镜头旋转改变偏移方向。FOV 固定值为 0 时对轨道 FOV 乘倍率，否则对固定值乘倍率；最终限制在 1–179°。暂停时同样支持更新，复位后继续原 VMD，停止恢复原始位置、旋转和 FOV。

七个机位参数和倍率保存进工程。新会话将初始参数放入 `camera-initial.txt`，在安装钩子前读取，从首帧生效；实时命令兼容旧七参数格式（倍率默认 1）和新八参数格式。构建清单标记 `cameraAdjustments`，对旧构建拒绝非单位倍率与初始调整。全局调整不写回源 VMD，也不烘焙进运镜关键帧导出。

验证：717 项核心检查、195 项运行时模拟检查、20 项控制台/传输测试通过。覆盖暂停 VMD 上的偏移和倍率、倍率限幅、归零复位、停止恢复、旧命令兼容、无效倍率拒绝、初始参数分发和工程保存/读取。没有启动、注入或操作用户游戏；真实画面适配效果待用户测试。


## 2026-10-13 播放完成后保持末帧

构建 `build-studio-hold-end`，测试入口 `test-hold-end.cmd`（测试工程默认开启）。拍摄辅助页新增“播放结束后保持最后一帧”，保存到工程的 `holdAtEnd`，新会话以 `hold-end.flag` 分发。默认关闭，旧工程仍自动恢复。

开启后 Timeline 仍发布 Ended 和动作末时刻，但不发起自然结束恢复；持续按姿态周期保持末帧，动画接管不释放。音频按 Ended 清理，不继续播放；相机接受保持中的 Ended 帧，维持对应末时刻机位，仍可应用偏移与 FOV 调整。界面显示“末帧保持中，点击停止并恢复结束”，保持停止、跳转、播放命令可用；播放从头重播，跳转改为暂停。循环开启时保持原循环行为，不进入末帧保持。

用户点击停止并恢复或关闭控制台时按现有恢复流程释放角色和相机。持帧会话取消原 10 分钟观察时限，加载器等待至会话明确结束，避免停留拍摄时被计时器恢复。异常和目标身份变化仍走恢复处理。

验证：717 项核心检查、201 项运行时模拟检查、21 项控制台/传输测试通过。覆盖自然结束持帧、超过原时限仍保持、手动恢复、默认自然结束恢复、相机末帧及恢复、选项分发与工程保存、GUI 持帧状态下停止命令。未启动或操作用户游戏，实机效果待用户测试。


## 2026-10-14 持续会话与游戏内运镜编排预览

构建 `build-studio-live`，入口 `test-live.cmd`。GUI 默认开启播放结束后保持会话（沿用末帧保持机制），动作自然结束不再释放相机、UID 或控制台连接。测试工程启用手动相机，方便无相机 VMD 时直接编排。点击停止并恢复或关闭控制台仍结束会话并恢复；旧工程缺少 holdAtEnd 字段时默认启用，显式 false 保留用户配置。

运镜页新增“游戏内预览此时刻”“游戏内播放运镜”“返回会话机位”。直接在内存中线性采样编排关键帧，并通过原子 camera.command 发送（约 30 Hz），不导出或导入 VMD 文件。预览覆盖相机轨道位置/旋转，沿原会话初始机位坐标应用；退出覆盖后恢复原轨道与用户偏移。预览独立于角色时间轴，从关键帧时间框指定的时刻开始；末帧保持，便于拍摄与修改。应用手动机位、停止会话或关闭 GUI 都会取消编排定时任务。原关键帧保存进主工程的 cameraKeys，也保留独立运镜工程与 VMD 导出能力。

UID 文字新增当前会话应用按钮，取消勾选后应用可请求恢复原文。worker 读取 UTF-8 文字，通过 SRWLock 快照传给原主线程 Text 写入/读回/恢复逻辑。首次会话不启用 UID 时仍验证接口，以支持后续动态启用。此变更新增实时传输，并不证明尚未验证的实际 UID 原生对象适配已成功；最终生效状态仍以运行时报告为准。

验证：719 项核心检查、204 项运行时模拟检查、22 项控制台/传输测试通过，包括实时预览命令兼容、覆盖原 VMD 轨道与返回、相机恢复、无 VMD 文件生成的编排预览、实时 UID 启用/恢复请求、默认保持会话和 GUI 布局。未启动或操作用户游戏，实机效果待测试。


## 2026-10-14 动态会话：独立角色、相机与退出

构建 `build-studio-dynamic`，入口 `test-dynamic.cmd`。连接并注入后处于待命，不自动播放 MMD，也不自动启用相机；连接时可不选择动作。动态会话由 `dynamic-session.flag` 启用，保持旧实验构建/协议行为以便回归。

- 播放/继续：待命时校验并载入当前选择的 VMD 和 PCM WAV（支持音频偏移），随后在主线程重新采集角色层级并绑定。MMD 结束默认暂停在末帧，不结束会话；继续可从头重播。
- 恢复游戏动画：仅恢复已写入的角色骨骼/位置/披风/辫子及 Animator；保留相机与 UID。每次再次播放重新验证角色和重新采集恢复基线。恢复未完成时不允许载入新动作。
- 开启相机/关闭相机并恢复：独立于 MMD，可以在角色正常游戏动画运行时使用。关闭并确认恢复后可切换当前相机 VMD 或手动机位，再次开启。相机轨道在 worker 解析，通过 SRWLock 队列交给主线程；主线程不读取文件。MMD 播放期间同步相机帧，角色恢复后保留相机最后时间，避免机位跳回首帧。
- 退出注入会话：单独的 exit 命令触发所有功能恢复，关闭消息/FOV 钩子与加载器。关闭控制台同样执行 exit。沿用原安全策略，DLL 映像仍保留到游戏进程退出，不声称已完成物理卸载；恢复失败则报告 pending 并保留必要回调。

命令新增 restore/exit/camera-on/camera-off，仍使用同一原子命令序号/确认机制。worker 仅在角色控制已释放时配置/载入新动作，再以原子标志交给主线程；待命时跳过骨骼写入。UID 与实验 BGM 的生命周期跟随动态会话退出，不再跟随 MMD 单次播放结束。运镜编排实时预览继续支持独立相机。

验证：723 项核心检查、214 项运行时模拟检查、23 项控制台/传输测试通过。覆盖待命不写角色、自然结束变为 Paused、恢复角色保留相机时间轴、关闭相机不停止 MMD、恢复动画不退出其他功能、无 MMD 的相机接管/关闭/重新开启及退出恢复、无动作启动、动态载入与独立命令分发。没有操作或注入用户游戏，完整实机往返仍待用户验证。

## 已预览未入队角色的静态演员（2026-10-15）

构建 `build-studio-actors-v7/test-actors.cmd`。在游戏中打开角色模型预览后，可在本地演员来源
输入 Vodyanitsa（沃雅妮莎）或 Odette（奥黛塔）生成副本。搜索支持队伍模型和已加载预览模型，
跨回调分批执行并保留命令确认语义。新增“检查演员”读取副本的网格及骨骼状态。

两种角色创建、激活、位置读回与清理已实机通过，奥黛塔十个蒙皮网格和骨骼归属检查通过。
关闭预览后的存续验证尚未完成；这版不声称任意未加载角色可直接载入，也不支持额外演员独立播放动作。
验证：723 核心、233 合成宿主、26 GUI/传输检查通过。
