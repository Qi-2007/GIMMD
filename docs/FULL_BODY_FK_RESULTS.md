# 全身 FK 与播放卡顿修复（2026-10-13）

已在原神当前男主模型上完成 52 个目标骨骼的完整旋转幅度播放，包含躯干、双臂、腿部和手指，并与 WAV 联动。用户反馈整体动画效果“好像还行”；首次播放明显卡顿，修复后反馈“明显改善，基本流畅”。

当前使用 EIEM 的默认 Tda 源预设近似与已验证的原神目标参考骨架，尚未提供源 PMX。Root/Center 位移、脚部 IK、捩骨、表情和相机未接入，因此不宣称动作精确复现。

## 全身 FK

- 源坐标基采用 EIEM `direct_vmd_pose.h` 的 Y 轴 180° 旋转；躯干和上肢使用默认源方向对齐，手腕/手指使用默认手掌参考框架对齐。
- 把源四元数的旋转轴换算到目标父级参考坐标系，再将旋转增量前乘到目标参考局部旋转。负缩放下按轴向量的反射规则处理，没有直接丢弃缩放符号。
- 开始时从捕获的游戏姿态用半秒过渡到 FK 输出；之后没有原先的 5° 幅度限制。初始游戏姿态仅用于过渡和停止恢复，不作为绑定姿态。
- 该文件有 51 个匹配源轨道；缺失的 `上半身3` 对应 Spine2 保持参考局部旋转。运行时验证全部 139 条目标 Avatar 参考记录，模型或参考数据不匹配时拒绝播放。

首次完整播放：499 批、25,948 次动作写入，25,896 次相邻姿态检查无覆盖；52 个骨骼和 Animator 全部恢复。按用户要求重播一次，500 批，无覆盖且恢复成功。该版本暴露了路径解析的性能问题。

## 卡顿原因与修复

旧实现每个骨骼都从 AvatarRoot 开始重复查找和验证。52 条路径共有大量前缀，实际每批重复处理约 661 个路径节点，并重复创建/读取名称字符串，耗尽游戏线程时间。

现在一次批处理只查找角色根一次，共有前缀在本批内复用；每个唯一节点仍验证名字、原生身份、父子关系和所属对象。52 个目标实际只需验证 **57 个唯一节点**，复用 **604 次前缀**。缓存只存在于当前回调栈，批次结束后不保存引擎对象指针。骨骼写入前仍完成全部目标预检；恢复也使用同样的批次解析。

Animator 租约检查改为跟随姿态更新节奏；紧急停止和恢复不节流。全身模式目标更新间隔从 33 ms 改为 16 ms，VMD 在关键帧间插值。首次完整骨架采样预算为 24 ms，仅在绑定时执行，以减少原有 12 ms 预算导致的启动拒绝。

使用相同 DLL 的两个新目录对照：旧解析模式播放约 3 秒，优化模式播放完整动作。测量的是游戏线程内一批姿态解析、采样和写入的耗时，不是渲染 FPS。

| 指标 | 旧解析路径 | 优化路径 |
|---|---:|---:|
| 测量批次数 | 66 | 934 |
| 平均每批耗时 | 43.49 ms | 2.59 ms |
| 最大每批耗时 | 57.25 ms | 3.93 ms |
| 平均耗时改善 | — | 约 16.8 倍，减少 94% |

优化版完整播放 23.469 秒，934 批、48,568 次动作写入、48,516 次相邻检查，覆盖为 0；52 次骨骼恢复成功，最大四元数分量恢复误差 5.96×10⁻⁸，Animator 恢复。音频 API 错误为 0，设备报告位置与时间轴最大偏差 54.65 ms；没有测量扬声器实际发声延迟。Hook 已停用、入口字节恢复、消息 Hook 移除，音频设备关闭。

[性能对照](evidence/resolver-benchmark.json)、[优化版实机报告](evidence/genshin-7.1-full-body-fast.json)、[首次全身播放](evidence/genshin-7.1-full-body-fk.json)、[重播报告](evidence/genshin-7.1-full-body-replay.json)、[验证清单](evidence/full-body-validation.json)。

## 构建与使用

```powershell
# 目标参考更新后才需重新生成；当前仓库已包含生成结果
python .\scripts\generate-fk-profile.py
.\scripts\build.ps1 -RuntimeProbe -SingleTrackPlayback -AnimatorLease -MultiTrackPlayback -FullBodyFK -PlayerSession -OutputDirectory build-full-next
python .\scripts\prepare-motion.py "testmmd/It's me x mosi mosi" .\build-full-next\gimmd-vmd.exe .\build-full-next\admission.json --stage .\build-full-next --full-body-fk --player-session --choreography ILLIT --motion-author Aileen_71 --author-notified

# 使用与游戏匹配权限的终端；替换实际 PID
.\build-full-next\gimmd-inject-probe.exe --pid 6020 .\build-full-next\gimmd-probe.dll
.\scripts\player-control.ps1 -Directory .\build-full-next -Command pause
.\scripts\player-control.ps1 -Directory .\build-full-next -Command play
.\scripts\player-control.ps1 -Directory .\build-full-next -Command stop
```

`-FullBodyFK` 默认关闭，要求多轨/Animator 租约模式，自动启用目标参考读取；CMake 为 `GIMMD_FULL_BODY_FK=ON`。省略 `-PlayerSession` 则仍是两秒实验；启用后按完整源时间轴播放，90 秒墙钟上限、结束恢复和现有控制命令沿用播放会话。每个 DLL 仍只消费一次，需要新目录重播。`legacy-resolver.flag` 仅用于本轮基线对照，默认不存在，应使用默认共享解析路径。

素材署名：原振付 **ILLIT**；动作 **Aileen_71**。用户已告知作者并授权实机测试；原始 VMD/WAV 没有修改或纳入源码分发。

本轮 668 项核心检查和 134 项运行时检查通过。新增检查集中于轴转换/乘法顺序、缺失源轨道、全身应用与恢复、参考不匹配、半批异常和初始过渡的暂停/继续。性能实测对应 `build-full-body-fast`；最终 `build-full-body-final` 仅追加了初始半秒过渡随暂停冻结的修正，并通过上述检查，没有额外重复完整实机播放。

下一步先接 Root/Center 位移，再处理脚部 IK。离线 17 点采样显示，该素材 Center 相对首帧位移约 7.16 个 MMD 单位，左右足 IK 位移也明显变化；这些位置通道尚未应用。源腿部 FK 本身含大幅旋转，不能简单把所有脚部问题归因于缺少 FK。

后续进展：[Root/Center 视觉骨架位移](ROOT_MOTION_RESULTS.md) 已完成实机播放和恢复，用户确认基本自然、流畅；本页保留全身 FK 阶段的测量记录。
