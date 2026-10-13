# 独立双腿 IK 与支撑脚可达补偿（2026-10-13）

在 52 骨骼 FK、视觉根位移与音频会话上加入独立双骨腿部求解。参考 EIEM 的足 IK 目标、开关轨道与支撑脚根部可达补偿思路；没有调用终末地 FinalIK，也没有假定原神存在同名组件。

## 当前实现

- 识别左右足 IK 的全角/半角 CP932 名称，保留 VMD Bezier 位置插值和四元数插值，遵守对应 IK 开关；关闭或缺失的一侧保留 FK。
- 在已验证的 Avatar 参考 TRS 上重建当前姿态，纳入实际冻结的 `Bip001` 局部旋转。每批核对 Pelvis 与双腿的实际局部位置、缩放以及根缩放；不同腿长、剪切或不匹配布局会拒绝求解。
- 用解析双骨求解计算大腿、小腿旋转，以随骨盆转动的参考前向作为膝盖极向。通过完整父级矩阵的逆变换处理方向，保留负缩放。脚踝旋转独立追随足 IK，脚尖 IK 尚未接入。
- 足 IK 保留原始位置偏移，与 Root/Center 共用根轨道首帧原点；足 IK 不分别清零。AllParent 的平移传递到足目标，Center/Groove 的位移由腿部补偿。
- 低足目标按源高度在 0.02～0.06 个换算后模型单位之间平滑释放支撑权重。需要时降低视觉骨架根，使支撑脚落在腿长可达范围，单次最多 0.25 个模型单位；剩余不可达部分限距，不拉伸腿骨。这是平地近似，没有地形射线、碰撞体或真实接地检测。
- FK 与 IK 合成后再进行半秒入场过渡，位置补偿使用相同过渡。暂停、跳转和循环沿用时间轴。全部引擎访问与现有批量预检、异常回滚、停止恢复共用游戏线程流程。

## 修正依据与离线结果

首版分别减去左右足轨道首帧位置，消掉了动作初始站姿，且不可达时直接限距。首轮实机完成 931 批、1,862 次单腿求解并全部恢复，用户反馈“变化不明显，仍有滑步或悬空”。这一反馈用于修正坐标基准，并加入支撑脚可达补偿。

同一素材按半帧采样整段动作（1,409 个姿态、2,818 次单腿求解），使用目标参考根旋转进行离线对照：

| 指标 | 首版 | 修正版 |
|---|---:|---:|
| 限距的腿部样本 | 986（35.0%） | 131（4.6%） |
| 最大请求目标偏差 | 0.19958 | 0.05944 |
| 可达目标最大计算残差 | 3.75×10⁻⁷ | 3.78×10⁻⁷ |
| 最大视觉根降低 | 0 | 0.05709 |

偏差单位为参考模型空间单位；计算残差只证明求解器输出满足数值目标，不代表游戏画面脚底接地误差。实际冻结的根旋转与参考旋转不同，实机限距结果可能不同。

[首版离线结果](evidence/leg-ik-offline-first.json)、[修正版离线结果](evidence/leg-ik-offline.json)、[首版实机报告](evidence/genshin-7.1-leg-ik-first.json)。

## 构建与检查

```powershell
.\scripts\build.ps1 -RuntimeProbe -SingleTrackPlayback -AnimatorLease -MultiTrackPlayback -FullBodyFK -RootMotion -LegIK -PlayerSession -Test -OutputDirectory build-ik-next
python .\scripts\prepare-motion.py "testmmd/It's me x mosi mosi" .\build-ik-next\gimmd-vmd.exe .\build-ik-next\admission.json --stage .\build-ik-next --full-body-fk --root-motion --leg-ik --player-session --choreography ILLIT --motion-author Aileen_71 --author-notified
.\build-ik-next\gimmd-ik-inspect.exe "testmmd/It's me x mosi mosi/Motion.vmd"
```

`-LegIK` 默认关闭，要求 `-RootMotion`；CMake 对应 `GIMMD_LEG_IK`。加载及控制操作沿用[全身会话命令](FULL_BODY_FK_RESULTS.md)。每次实机重播使用新目录，已加载 DLL 不原地覆盖。

681 项核心检查、148 项运行时检查通过，覆盖可达/不可达目标、腿长保持、负缩放方向换算、脚踝朝向、Center 补偿、支撑权重、IK 开关、外部根旋转变化、实际腿长不匹配以及半批异常后的骨骼/位置/Animator 恢复。

仍使用默认 Tda 源预设近似，无源 PMX 精确校准。半标准足 IK 父级、脚尖 IK、捩骨和地形适配尚未支持；Root 控制骨旋转继续明确拒绝。素材原振付 ILLIT，动作 Aileen_71；用户已告知作者并授权实机测试，原始素材未修改、未纳入 Git。

## 实机验证状态

首版实机每批平均耗时 3.99 ms、最大 5.17 ms，无旋转或位置覆盖，52 个旋转目标、视觉根与 Animator 均恢复，回调和音频资源已清理。

修正版 `build-leg-ik-support` 已通过上述检查及整段离线采样；加载尝试的 Windows UAC 被取消，尚未注入游戏，画面效果待验证。不能用离线偏差下降宣称滑步或悬空已经解决。状态详见[验证清单](evidence/leg-ik-validation.json)。
