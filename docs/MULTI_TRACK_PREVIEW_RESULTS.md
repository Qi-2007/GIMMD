# EIEM 功能移植：真实 VMD 导入与十骨骼预览

2026-10-13，原神国服 7.1.0，PID 6020。接续 [Animator 接管](ANIMATOR_LEASE_RESULTS.md)。

**已将真实 VMD 的十条骨骼旋转轨道接入正式客户端，并完成批量姿态恢复。当前仍是前两秒、每骨骼最多偏转 5° 的相对旋转预览，不是完整舞蹈重定向。**

## 素材与导入

用户提供本地目录 `testmmd/It's me x mosi mosi`，包含 Motion.vmd、WAV 与 Readme。用户已确认按 Readme 要求告知作者后，才进行了该素材的客户端测试。素材不纳入源码发布；`testmmd/` 和构建目录已加入忽略规则。导入清单保留原路径、文件指纹和署名，未改写原素材。

发布使用该动作的作品时，按原说明保留以下署名：

原振付：ILLIT

动作：Aileen_71

VMD 包含 252 条骨骼轨道、37,967 个骨骼关键帧、11 条表情轨道，无相机关键帧和 IK 开关轨道，最后帧 704，约 23.47 秒。没有 IK 开关轨道不代表没有 IK 目标骨骼轨道，本轮没有做腿部 IK 解算。WAV 为 48 kHz、双声道、16 bit PCM，约 23.53 秒。未播放音频，也未实现音画同步。

新增 `prepare-motion.py`：调用现有 VMD 检查器，按 CP932 原始字节匹配 52 项暂定映射，生成包含资源、音频元数据、署名和能力边界的导入清单；该文件精确匹配 51 项，其中本轮选取的 10 项全部存在。匹配只是名称兼容，不代表轴向和 bind pose 已校准。证据见 [素材导入清单](evidence/its-me-motion-admission.json)。

## 本轮实现

新增共享骨骼映射 profile、相对旋转采样核心与十轨播放策略。十个目标为左右上臂、前臂、手腕、颈、头及左右食指第一节。源名称为标准日文 CP932 VMD 名称，不再依赖测试专用的 `left_arm_test`。

逐轨从原素材采样，以该轨源第 0 帧为参考，计算 `inverse(sourceFrame0) * sourceSample`；将最短旋转限制到 5°，再叠加到停用 Animator 后捕获的目标局部姿态。此处捕获的是播放前姿态，不称为中性姿态或 bind pose，也不证明原神与 MMD 的局部轴一致。

所有轨道使用同一毫秒时钟和 VMD 30 fps 时间基准。每批先重新解析全部目标路径、核对身份并读取姿态，再开始写入；每个骨骼各自记录是否已写入。任意一条写入或读回失败，恢复本批已变更的目标；每个目标最多 8 次恢复写入。一个骨骼失联时，仍恢复其他可验证的目标并释放 Animator，避免整个角色一直停用。

目标必须处于同一个已验证 Animator 模型的子树内。发现其他系统覆盖任何目标姿态时停止本次预览并恢复，不继续累积偏移。

## 正式客户端结果

先使用自制十骨骼校准 VMD，再使用用户的真实 VMD；两个 DLL 映像的二进制相同，使用不同输出目录分别消费一次实验。

| 指标 | 自制校准 | 用户真实素材 |
|---|---:|---:|
| 两秒内完整写入批次 | 38 | 37 |
| 骨骼旋转写入，含停止恢复 | 390 | 380 |
| 相邻姿态保留检查 | 370 | 360 |
| 发现覆盖 | 0 | 0 |
| 恢复目标数 | 10 | 10 |
| 每目标恢复尝试 | 1 | 1 |
| 最大四元数分量恢复误差 | 0 | 7.45×10⁻⁹ |
| Animator 原状态恢复 | 成功 | 成功 |
| 异常 / 待恢复状态 | 0 / 无 | 0 / 无 |

证据：[校准实测](evidence/genshin-7.1-multi-calibration.json)、[真实素材实测](evidence/genshin-7.1-real-motion-preview.json)。两次 Hook 均撤销、入口字节恢复、活动回调为 0，没有调用 timeScale setter。检查数是轨道之间的回调观察次数，不是最终渲染帧数；没有画面录制或视觉验收。

真实素材的多个目标几乎每次采样都触发 5° 限制，因此结果会明显压缩原动作。其他 242 条骨骼轨道、11 条表情轨道和全部位置通道未应用。这是明确的预览范围，不代表素材本身缺少这些数据。

## 构建与使用

```powershell
.\scripts\build.ps1 -RuntimeProbe -SingleTrackPlayback -AnimatorLease -MultiTrackPlayback -Test -OutputDirectory build-multi-next
python .\scripts\generate-multi-track-fixture.py .\build-multi-next\multi-track.vmd

# 在新目录构建后导入真实素材；不要覆盖已消费的实验目录
python .\scripts\prepare-motion.py "testmmd/It's me x mosi mosi" .\build-multi-next\gimmd-vmd.exe .\build\motion-admission.json --stage .\build-multi-next --choreography ILLIT --motion-author Aileen_71 --author-notified
```

`-MultiTrackPlayback` 要求 `-AnimatorLease` 与 `-SingleTrackPlayback`；CMake 为 `GIMMD_MULTI_TRACK_PLAYBACK`，默认关闭。此模式读取 DLL 同目录 `multi-track.vmd`，文件上限 16 MiB，要求十个源轨道全部存在。原有单轨实验仍读取 `single-track.vmd`。

已通过 661 项核心检查与 123 项运行时检查，覆盖源参考消除、最短旋转和角度限制、十轨同步写入、半批异常回滚、全目标预检、身份失联与恢复、每目标恢复上限及外部覆盖时退出。普通 Animator 单轨版本回归通过 92 项运行时检查，只读版本通过 53 项；均通过核心、CLI 和未知 EXE 拒绝检查。另核对了生成夹具、原素材与暂存副本一致性及署名清单。指纹与回归结果见 [验证清单](evidence/multi-track-validation.json)。

## EIEM 剩余功能状态

| 功能 | 当前状态 | 下一步 |
|---|---|---|
| VMD 骨骼导入与插值 | 后续已扩展 52 目标全身/手指 FK，51 个源轨道，缺失保持参考 | [全身 FK 实测](FULL_BODY_FK_RESULTS.md) |
| Animator 控制权与停止恢复 | 单轨、十轨通过有界实测 | 长时会话与场景切换验证 |
| 正式动作重定向 | 目标参考与默认源预设近似的全身 FK 已实测 | 源 PMX 精确校准、IK 和捩骨 |
| Root/Center/位置轨道 | 视觉骨架根位移已实测，按腿长比例换算并恢复 | [位移实测](ROOT_MOTION_RESULTS.md)；控制骨旋转/枢轴仍未支持 |
| 腿部/脚尖 IK、捩骨 | 未接入 | 验证 FK 基准后接入独立求解，不假定存在 FinalIK |
| 暂停、跳转、循环、速率 | 后续已接入并实测暂停、跳转、继续和循环；速率未接入 | 见 [播放会话](PLAYER_SESSION_RESULTS.md) |
| 表情和眼睛 | 源数据已解析，未写入客户端 | 枚举原神面部驱动与可恢复通道 |
| 音频与同步 | 后续已接入 WinMM PCM 输出和时间轴联动 | 可听延迟测量、设备变化和生产后端 |
| VMD/自由相机与 FOV | 相机只读观察已有，未控制 | 识别最终游戏相机和最后写入阶段 |
| 作品库、工程、界面 | 资源导入清单已加入；无创作界面 | 持久工程、演员绑定配置与控制面板 |
| 服装物理、HUD、多人 | 未验证 | 分别确认原神组件与生命周期 |

继续以 [已获取的 EIEM/BE 源码快照](../references/SOURCE_MANIFEST.json) 为设计依据；本轮新增桥接和批量恢复实现，没有直接调用终末地的组件或偏移。剩余功能还没有全部复刻完成。
