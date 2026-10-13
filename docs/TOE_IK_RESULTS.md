# 脚尖 IK 与实际腿部数据捕获（2026-10-13）

在独立腿部 IK 后加入脚尖目标：固定已求解的脚踝位置，通过旋转脚踝让脚掌骨段朝向脚尖 IK 的位置目标。参考 EIEM `DirectVmdSolveToeAim` 及求解后脚踝调整的顺序，使用本项目的数值求解与局部旋转接口。

## 行为与边界

- 支持左右脚尖 IK 的 CP932 全角/半角名称、位置插值及 IK 开关。足 IK 关闭时也关闭该侧脚尖求解；脚尖轨道缺失或关闭时保留现有脚踝结果。
- 把脚尖目标当作足 IK 的子目标，按足 IK 旋转变换参考脚掌方向和源位置偏移。以已经限距的实际脚踝为父级，防止零脚尖位移因腿部不可达而额外转动脚掌。
- 只修正脚踝旋转，不移动脚踝、不拉伸脚掌；Toe0 的 FK 局部旋转继续沿用。脚尖 IK 控制骨的旋转没有作为位置约束应用。退化目标保留原脚踝结果并计数。
- 零脚尖位移直接保留已验证脚踝输出。骨段固定长度时，径向不可达部分不强行命中，报告方向误差与径向差，不能把方向对准宣称为三维位置完全重合。
- 继续沿用入场过渡、支撑脚根部补偿、批量预检、外部覆盖检测和停止恢复。没有增加游戏接口、引擎组件或跨帧对象指针缓存。

## 捕获实际局部数据

首轮预检发现当前局部数据偏离参考约 0.00084；后续读到骨盆偏移约 0.00443，均在姿态写入前拒绝并恢复 Animator。一次参考方法只读采样也因 12 ms 预算不足而拒绝，未写入姿态。

现在在获得 Animator 控制权后捕获 Pelvis、双腿与双脚尖的实际局部位置和缩放，用这组数字重建求解骨架。骨盆是动画位移通道，允许参考附近 0.15 个局部单位的位置偏移；骨段位置限制为 0.002，缩放差为 0.003。捕获后每批以 0.00002 的分量阈值确认局部数据不变，变化时停止恢复。全量 139 条 Avatar 参考身份/数据验证仍沿用原流程；不使用当前动画旋转作为参考绑定姿态。

## 离线验证

自制六秒校准：中性 FK 和足 IK，左脚在 1.5～2 秒抬脚尖，右脚在 4～4.5 秒抬脚尖，随后回零。最大源脚尖位移 0.5 个 MMD 单位，附六秒静音 WAV。没有修改第三方动作。

- 361 个半帧姿态、722 次脚尖求解，178 个产生大于 0.05° 的旋转修正。
- 最大脚踝修正约 14.38°，最大方向向量误差 1.84×10⁻⁷，固定脚掌长度造成最大径向差 0.02032 个模型单位。
- 原舞蹈完整 1,409 个半帧姿态、2,818 次脚尖求解，修正次数为 0；52 个旋转输出与上一版逐姿态对比最大分量差为 0。该素材的脚尖位置轨道全段为零，不能靠重播它验证主动抬脚尖。

[校准生成清单](evidence/toe-calibration.json)、[校准离线结果](evidence/toe-ik-calibration-offline.json)、[原舞蹈零位移回归](evidence/toe-ik-motion-offline.json)。

689 项核心检查、156 项运行时检查通过，包含脚尖方向、脚踝位置保持、脚掌长度保持、零位移不改变输出、退化目标、IK 开关、骨段不匹配、实际局部数据捕获/变化及半批异常后的恢复。

## 构建与校准

```powershell
.\scripts\build.ps1 -RuntimeProbe -SingleTrackPlayback -AnimatorLease -MultiTrackPlayback -FullBodyFK -RootMotion -LegIK -ToeIK -PlayerSession -Test -OutputDirectory build-toe-next
python .\scripts\generate-toe-calibration.py .\build-toe-next
.\build-toe-next\gimmd-ik-inspect.exe .\build-toe-next\multi-track.vmd --toe-ik
```

`-ToeIK` 默认关闭，要求 `-LegIK`；CMake 对应 `GIMMD_TOE_IK`。加载与控制沿用[全身会话命令](FULL_BODY_FK_RESULTS.md)。原舞蹈暂存使用 `prepare-motion.py` 并追加 `--toe-ik`，生成校准动作与导入舞蹈应使用不同的新目录。

源 PMX 校准、半标准足 IK 父级、捩骨、地形射线与真实接地仍未实现。本轮原舞蹈只作离线回归，原振付 ILLIT、动作 Aileen_71，原始素材未修改、未纳入 Git。

## 实机状态

最终 `build-toe-ik-final` 的 UAC 被取消，尚未加载，主动抬脚尖的画面效果待验证。先前的预检拒绝报告均为 0 次姿态写入，无异常或待清理状态，Hook 和音频资源已清理；这些拒绝不能算作脚尖动作实机验证。

[验证清单](evidence/toe-ik-validation.json)、[初次预检拒绝](evidence/genshin-7.1-toe-ik-refused.json)、[元数据预算拒绝](evidence/genshin-7.1-toe-ik-metadata-partial.json)、[骨盆预检拒绝](evidence/genshin-7.1-toe-ik-pelvis-refused.json)。
