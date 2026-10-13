# Root/Center 视觉骨架位移（2026-10-13）

在全身 FK 基础上加入整体位移，原神国服 7.1.0 当前男主模型已完整播放 23.469 秒。用户反馈“基本自然、流畅”。结束后 52 个骨骼旋转、骨架根位置与 Animator 均恢复。

## 实现范围

读取 `全ての親`、`センター`、`グルーブ` 的位置轨道，按首帧锚定后求和。沿用 EIEM 默认源坐标基 Y 轴旋转 180°，以目标与默认源腿长比例换算单位（0.0748706907），再应用 Body 的局部旋转和带符号缩放，写入模型的视觉骨架根 `Bip001`。起播使用与 FK 相同的半秒过渡。

本轮没有移动实体、碰撞体或游戏逻辑坐标，没有源 PMX 精确校准。控制骨的旋转与枢轴组合尚未实现；存在非单位控制骨旋转的动作会明确拒绝，不能把旋转层级下的位置简单相加。脚部 IK、脚尖 IK 和捩骨仍未接入。

位置读写接口由 Transform 方法元数据确认，逐层核对 managed 包装、原生读写及 TRS 存储代码，并固定 EXE 指纹与入口字节。所有游戏对象访问在游戏线程执行，位置写入与全身旋转共享本批路径缓存。外部位置覆盖、模型身份变化或接口异常会停止播放并尝试恢复；不会为下一批保存对象指针。

## 实测

| 指标 | 结果 |
|---|---:|
| 更新批次 | 933 |
| 旋转动作写入 | 48,516 |
| 根位置动作写入 | 933 |
| 根位置相邻保留检查 | 932，覆盖为 0 |
| 最大根位置偏移 | 0.59664517 个根父级局部单位 |
| 每批平均 / 最大耗时 | 2.94 / 5.59 ms |
| 根位置恢复 | 1 次，最大分量误差 0 |

52 个旋转目标均恢复，无覆盖；Animator 恢复，异常为 0，无待清理状态。音频 API 错误为 0，设备报告位置与时间轴最大偏差 37.02 ms，音频设备已关闭。回调 Hook 停用、入口字节恢复、消息 Hook 移除。批次耗时不是渲染 FPS，设备位置偏差不是扬声器实际发声延迟。

[实机报告](evidence/genshin-7.1-root-motion.json)、[验证清单](evidence/root-motion-validation.json)、[只读位置接口采样](evidence/genshin-7.1-position-methods.json)。

## 构建

```powershell
.\scripts\build.ps1 -RuntimeProbe -SingleTrackPlayback -AnimatorLease -MultiTrackPlayback -FullBodyFK -RootMotion -PlayerSession -Test -OutputDirectory build-root-next
python .\scripts\prepare-motion.py "testmmd/It's me x mosi mosi" .\build-root-next\gimmd-vmd.exe .\build-root-next\admission.json --stage .\build-root-next --full-body-fk --root-motion --player-session --choreography ILLIT --motion-author Aileen_71 --author-notified
```

`-RootMotion` 默认关闭且要求 `-FullBodyFK`，CMake 对应 `GIMMD_ROOT_MOTION`。加载及暂停、跳转、停止操作沿用[全身播放命令](FULL_BODY_FK_RESULTS.md)。已加载的 DLL 不原地覆盖，重复播放使用新目录。

671 项核心检查与 140 项运行时检查通过。新增覆盖坐标/单位换算、首帧锚定、整体位移应用与恢复、外部覆盖、部分写入异常及旋转控制骨拒绝。实机报告的恢复、覆盖、Hook 与音频关闭状态另经离线断言核对。

素材原振付：ILLIT；动作：Aileen_71。用户已告知作者并授权实机测试，原始素材未修改且未纳入 Git。
