# 时间轴控制与音频联动（2026-10-13）

后续已扩展到完整幅度的 52 目标骨骼 FK，并优化骨架解析，见 [最新实测](FULL_BODY_FK_RESULTS.md)。下文保留十骨骼阶段记录。

已把两秒十骨骼实验扩展为受控播放会话，接入真实 WAV 输出。客户端内已验证暂停、跳转、继续、循环和停止恢复。骨骼仍使用源第 0 帧相对旋转、每骨骼最多 5°；这不是完整舞蹈重定向。暂停控制的是本工具的动作和音频时间轴，不是游戏世界时间。

## 实机结果

国服 7.1.0，PID 6020，固定 EXE 指纹沿用前阶段。素材为用户已获准实机使用的 `It's me x mosi mosi`：动作 23.4667 秒，音频 23.5307 秒。署名：原振付 **ILLIT**；动作 **Aileen_71**。

最终一次会话依次执行：播放到约 1.1 秒 → 暂停并确认位置不变 → 暂停时跳转到 5 秒 → 继续 → 开启循环 → 越过结尾回到 0.61 秒 → 主动停止。

| 指标 | 结果 |
|---|---:|
| 游戏线程确认执行的命令 | 5 |
| 十骨骼同步批次 / 动作写入 | 385 / 3,850 |
| 相邻姿态检查 / 覆盖 | 3,840 / 0 |
| 骨骼恢复 | 10 / 10，各一次 |
| 恢复四元数最大分量误差 | 7.45 × 10⁻⁹ |
| Animator 恢复 | 成功 |
| 音频重新排队 / 设备位置采样 | 3 / 970 |
| 音频 API 错误 / 待恢复状态 | 0 / 无 |
| 设备报告位置与时间轴最大偏差 | 41.85 ms |

音频偏差来自 `waveOutGetPosition` 与软件时间轴对比，包含回调和工作线程调度误差；没有测量扬声器实际发声延迟，也没有视觉或听感验收。Hook 已停用、入口字节恢复、消息 Hook 已移除，音频设备已关闭。DLL 保持映射至游戏退出。

[最终实机报告](evidence/genshin-7.1-player-loop-stop-audio.json)、[命令状态采样](evidence/player-control-events.json)、[验证及指纹](evidence/player-session-validation.json)。前一次会话验证了暂停、跳转、继续和自然结束，见 [报告](evidence/genshin-7.1-player-pause-seek-audio.json)；其中 `durationMs:2000` 是旧报告字段，实际运行 81.563 秒（含暂停），最终构建已修正该字段。

## 使用

每个 DLL 仍只消费一次。使用新输出目录构建，不覆盖游戏已加载的 DLL：

```powershell
.\scripts\build.ps1 -RuntimeProbe -SingleTrackPlayback -AnimatorLease -MultiTrackPlayback -PlayerSession -OutputDirectory build-player-next
python .\scripts\prepare-motion.py "testmmd/It's me x mosi mosi" .\build-player-next\gimmd-vmd.exe .\build-player-next\admission.json --stage .\build-player-next --player-session --choreography ILLIT --motion-author Aileen_71 --author-notified

# 在与游戏匹配权限的终端加载；替换 PID，加载器等待会话结束
.\build-player-next\gimmd-inject-probe.exe --pid 6020 .\build-player-next\gimmd-probe.dll

# 播放过程中，从另一终端控制同一目录
.\scripts\player-control.ps1 -Directory .\build-player-next -Command pause
.\scripts\player-control.ps1 -Directory .\build-player-next -Command seek -Seconds 8
.\scripts\player-control.ps1 -Directory .\build-player-next -Command play
.\scripts\player-control.ps1 -Directory .\build-player-next -Command loop-on
.\scripts\player-control.ps1 -Directory .\build-player-next -Command loop-off
.\scripts\player-control.ps1 -Directory .\build-player-next -Command status
.\scripts\player-control.ps1 -Directory .\build-player-next -Command stop
```

`-PlayerSession` 需搭配十骨骼模式，默认关闭；CMake 对应 `GIMMD_PLAYER_SESSION=ON`。从 DLL 目录读取 `multi-track.vmd` 和 `multi-track.wav`，暂存脚本不修改原素材。音频支持单/双声道、16-bit PCM、8–192 kHz，文件上限 64 MiB。缺失或不支持的 WAV 不阻止无声动作预览，查看报告的 `audioLoaded` 和 `audioError`。

会话自动播放，到动作末尾自动恢复；开启循环后仍受 **90 秒墙钟上限**约束，暂停时间也计入。停止会恢复角色并结束本次会话，再播放需新目录的 DLL。状态枚举为 Ready=0、Playing=1、Paused=2、Ended=3；`acknowledgedSequence` 表示游戏线程已处理命令。命令文件是单槽邮箱，适合顺序操作，不是批量命令队列；连续命令应等待确认后再发下一条。没有播放速率调节或界面。

## 实现与检查

游戏线程独占 Timeline，通过一个原子整数接收命令，并向音频线程发布包含位置、状态和 epoch 的一致快照。seek 和循环改变 epoch，使音频停止旧缓冲、从新位置重新排队；暂停和继续分别控制音频设备。文件读取和音频调用都在工作线程，游戏线程仅做姿态验证与应用。音频采用 WinMM 原型后端，其暂停、重置和重启语义按 [微软文档](https://learn.microsoft.com/en-us/windows/win32/multimedia/stopping-pausing-and-restarting-playback) 实现。

本轮增加命令编码、非法输入、一致快照，以及运行时暂停、seek、循环、停止恢复检查；665 项核心检查和 127 项运行时检查通过。未重复只读、单轨等完整构建矩阵。控制脚本首次使用 `File.Replace` 的空备份路径在 PowerShell 下失败，改为覆盖式 `File.Move` 后完成上述联动；脚本失败时也会发送停止命令。

下一阶段仍以全身重定向为主：可信目标中性姿态/局部轴校准、Root/Center 位移和腿部 IK。表情、相机、服装物理及创作界面仍未实现；不应通过简单放大当前相对旋转来宣称完成舞蹈移植。
