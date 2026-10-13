# 两秒 Animator 接管与单轨对照

2026-10-13，原神国服 7.1.0，PID 6020。接续 [Animator 接口验证](ANIMATOR_INTERFACE_RESULTS.md)。固定 EXE SHA-256 沿用 `7F89938DA606C1281659607D464702CDDB9A09A7D4320A196630F60A811EC38E`。

**当前测试上臂的跨回调覆盖问题已在两秒实验中解决：Animator 停用期间没有发现姿态覆盖，退出后骨骼和组件原状态恢复成功。** 这仍是单骨骼、五度合成轨道的数值验证，尚不是完整舞蹈或画面验收。

## 同一进程中的实测对照

| 指标 | 普通单轨，Animator 启用 | 接管单轨，Animator 停用 |
|---|---:|---:|
| 写入采样数 | 42 | 43 |
| 相邻姿态数值保留 | 0 | 43 |
| 发现骨骼覆盖 | 42 | 0 |
| 骨骼 setter 调用，含恢复 | 43 | 44 |
| Animator setter 调用 | 0 | 2 |
| 骨骼恢复误差 | 0 | 0 |
| 异常 | 0 | 0 |

原始报告：[普通播放对照](evidence/genshin-7.1-animator-lease-control.json)、[Animator 接管](evidence/genshin-7.1-animator-lease-playback.json)。

两次使用同一游戏进程、同一 `left_arm_test` 合成 VMD、同一上臂选择规则、两秒时长及约 33 ms 的写入间隔。先完成接管实验，再运行普通播放对照；对照中重新出现覆盖，也支持退出后动画写入已恢复。它们不是同时运行或逐帧配对：接管版额外使用独立消息调度，普通版由 FOV 回调驱动，因此采样数有一项差异，不能把差异当作精确性能统计。

“姿态保留/覆盖”在下一次采样及最终恢复前比较当前局部四元数与上次写入值，容差为分量误差 `0.00002`。计数不等同于渲染帧数，也不能直接证明每个渲染帧的最终画面。

接管版收到 101 次私有消息回调，296 次 Animator 状态检查均保持停用，没有外部 enabled 覆盖。Animator 原状态为 true，启停 setter 共 2 次，恢复尝试 1 次，恢复读数为 true；没有调用 timeScale setter。骨骼恢复尝试 1 次，组件和骨骼均无待恢复状态。两次报告的 Hook 均已撤销、入口字节恢复、活动回调为 0。

本次唯一 Animator 位于模型节点 32 `Avatar_Boy_Sword_PlayerBoy`。节点号仅属于本次扫描，运行时不依赖该常数。

## 实现与恢复顺序

新增 `src/windows/animator_lease.h`，默认关闭。完整骨架扫描后，先只读确定唯一角色 Animator、所属对象、父类、方法名称、参数数及固定入口，再绑定其模型路径；只有该模型也是目标骨骼的祖先才允许开始实验。

每次回调重新解析模型路径和组件，核对路径节点身份、类、owner 和 Animator native 身份。跨回调只保存路径、名称及用于比较的身份整数，不解引用保留的对象地址。组件启用状态若原本为 false，拒绝接管；不会在退出时误开启原本停用的组件。

首次停用前要求独立 `WH_GETMESSAGE` 回调至少执行 3 次，随后保存 enabled 原值、停用并复读。播放由 `GetTickCount64` 推进；同一已确认主线程负责姿态写入与恢复。停止时先恢复骨骼，再恢复 enabled 原值并复读。发现外部 enabled 改动时停止实验并恢复。

模型路径是骨骼路径的前缀，恢复 Animator 不依赖末端测试骨骼。若骨骼身份失联，先尝试恢复 Animator，再等待原骨骼身份恢复处理姿态；不会对替换后的骨骼或组件调用 setter。若骨骼恢复写入耗尽 8 次上限，也尝试释放 Animator，避免一直停用角色动画。

恢复失败不报告成功：保留待恢复状态和必要回调，setter 重试最多 8 次，目标身份恢复后可继续清理；若最终无法恢复，需要退出客户端。异常路径、角色切换和身份变化已在合成宿主中验证，**未在正式客户端故意触发这些失败**。

## 构建与测试

```powershell
.\scripts\build.ps1 -RuntimeProbe -SingleTrackPlayback -AnimatorLease -Test -OutputDirectory build-animator-lease-next
python .\scripts\generate-single-track-fixture.py .\build-animator-lease-next\single-track.vmd
```

`-AnimatorLease` 自动启用 Animator 名称观察，要求单轨实验，与 `-FreezeTime`、`-AnimatorToggle` 互斥。CMake 对应 `GIMMD_ANIMATOR_LEASE`，同样默认 OFF。已加载的 DLL 不得原地覆盖，应使用新目录。

正式客户端加载版本通过 657 项核心检查与 88 项运行时检查，普通播放对照通过 657 项核心检查与 56 项运行时检查。后续仅补充恢复上限、拒绝未命名 setter 和骨骼恢复耗尽后的组件释放断言及一处注释，另在新目录构建最终版本，通过 92 项运行时检查；最终只读版本通过 53 项运行时检查。上述各版本均通过核心、CLI 和未知 EXE 拒绝检查。最终版本未重复注入；二进制与证据指纹见 [验证清单](evidence/animator-lease-validation.json)。

测试包括持续停用、独立调度未就绪时拒绝写入、原本停用时拒绝接管、部分 setter 异常恢复、恢复失败重试、外部 enabled 覆盖、失去骨骼或 Animator 身份、恢复身份后的清理、角色 inactive 后恢复原状态、重试上限及消息调度下的退出恢复。

## 尚需完成

下一步扩展到多骨骼和真实 VMD 动作，验证旋转基准、位置轨道、根运动、角色切换及可见画面。当前只证明选定上臂在本次两秒场景中不再被覆盖；其他骨骼可能仍受 IK、注视、程序化动画或次级物理影响，不能据此宣布全身接管完成。
