# 动画控制权：暂停时间与 Animator 定位实测

2026-10-12，原神国服 7.1.0，PID 9620。接续 [单轨驱动实测](SINGLE_TRACK_PLAYBACK_RESULTS.md)。

本轮按用户提出的方向实测了 `timeScale=0`，并定位到当前角色的唯一 Animator。结论是：**归零时间比例仍无法阻止骨骼姿态被回写，接下来应验证组件级动画接管。** 尚未停用 Animator，也未验证稳定的可见舞蹈。

后续进展：2026-10-13 已完成按名称定位 enabled 接口、只读状态采样和同一回调的停用/恢复，见 [Animator 接口实测](ANIMATOR_INTERFACE_RESULTS.md)。以下保留此前阶段的结果；尚未完成跨帧接管。

## EIEM 的做法与适配决定

参考 [EIEM animation.h](https://github.com/Sasye/EIEM/blob/main/src/animation.h) 的 `SafeSetAnimatorEnabled`，以及本地快照 `references/EIEM_ghost_rig.h` 中的 `GhostRig_BeforeFinalIK`：DirectVmd 路径停用 Unity Animator，在目标角色的 IK 求解阶段写入动作，并按组件归属处理其他骨骼写入者。Better-Endfield 的固定快照 `references/BE_eiem_slot.inc` 同样在进入 DirectVmd 时停用 Animator。

应移植“取得动画控制权、退出时恢复”的机制；原神中的组件、时钟和更新顺序仍需独立核对，不能直接套用终末地的类名或偏移。实现应保存每个组件的原启用状态，而非退出时一律设置为 true。

Unity 的 [非缩放 Animator 模式](https://docs.unity3d.com/kr/2017.2/ScriptReference/AnimatorUpdateMode.UnscaledTime.html) 可以独立于 timeScale 更新。不过，本轮覆盖计数并不能确认原神正在使用该模式：即使时间不再推进，动画系统也可能重新求值并回写同一个冻结姿态。需要区分“动作时间停止”和“停止写入骨骼”。

## 时间比例接口的证据

本机固定 EXE 的错误字符串明确指出 `Time.timeScale` 的合法范围。只读交叉引用及调用链复核得出：

| 接口 | RVA / ABI | 依据 |
|---|---|---|
| TimeScale getter | `0x1436bd0`，`float()` | 从 TimeManager 同一字段 `+0x104` 返回 float |
| TimeScale setter | `0x1436bf0`，`void(float)` | 保留 xmm0 参数，取得 TimeManager，再转成成员调用 |
| 底层 setter | `0x16f3270` | 检查 0–100 范围，写入上述字段；错误路径引用明确的 Time.timeScale 消息 |
| TimeManager getter | `0x16f2660` | 上述 getter/setter 共用 |

已加入固定 SHA-256 与入口 32 字节核对。证据见 `evidence/time-scale-string-research.json`、`time-scale-native-disassembly.json`、`time-scale-wrapper-disassembly.json`。邻近函数和不对齐的显式解码项仅是研究输出，不作为可调用接口。

## 两秒 timeScale 属性归零实测

[原始报告](evidence/genshin-7.1-freeze-time-playback.json)：

| 指标 | 未暂停的上一阶段 | 本轮 timeScale=0 |
|---|---:|---:|
| 单轨采样写入 | 43 | 43 |
| 相邻采样发现姿态覆盖 | 42 | 42 |
| 相邻采样数值保留 | 1 | 1 |
| 局部姿态恢复误差 | 0 | 0 |

本轮时间比例实际经历 `1 → 0 → 1`；暂停期间 338 次检查均读到 0，未发现时间比例被游戏重新设置。时间 setter 共调用 2 次，恢复尝试 1 次，异常为 0。338 是回调检查次数，不是渲染帧数。

为了让停止恢复不依赖 Unity 的缩放时间，新增主线程 `WH_GETMESSAGE` 调度：仅消费自身发布的私有线程消息，其他消息继续原流程。首次归零前要求消息回调已确认执行至少 3 次。VMD 时间由 `GetTickCount64` 推进；停止请求、时间恢复和骨骼恢复都在同一已确认线程执行。实测收到 101 次私有消息回调，FOV Hook 和消息 Hook 均已撤销，原入口字节恢复，报告时活动回调为 0。

首次准备因完整骨架扫描超过 12 ms 而拒绝执行，没有任何时间或骨骼 setter 调用，记录保留于 [拒绝报告](evidence/genshin-7.1-freeze-time-preflight-refused.json)。第二次才执行暂停实验。

用户随后观察到：`timeScale=0` 时角色和怪物仍在运动。因此本次实验**未实现实际场景暂停**，不能把属性读数归零称为暂停成功。这个结果只确认该属性的写入和恢复链路；原神实际使用的动画/场景时钟尚未确定，不能据此认定使用了 Unity 非缩放模式。

## 唯一 Animator 的只读验证

[原始报告](evidence/genshin-7.1-animator-observer.json)：

- 查询当前唯一 active Avatar 子树中深度不超过 3 的节点，共 15 次。
- 找到 1 个匹配 Animator，挂载在节点 20：`Avatar_Boy_Sword_PlayerBoy`，位于当前角色的 `OffsetDummy` 下。
- 返回对象的类指针与明确注册为 `UnityEngine.AnimationModule.dll / UnityEngine / Animator` 的缓存一致，且 `component.gameObject` 与查询节点所属对象一致。类型/owner 不匹配数为 0。
- 没有调用任何 setter；无异常；观察 Hook 已停用，入口字节恢复，活动回调为 0。

组件查询包装器为 `0x1453dd0`：接受组件及已验证的 managed string，取得原生 GameObject 后调用 `0x14c8660`，完成名称查找、组件查询和 managed object 转换。类型注册与缓存写入证据分别来自 `0x1b0a401` 和 `0x1b0a435`；本轮只读缓存指针槽 `0x56ce298` 所指结构的 `+0x50`。所有调用及证据入口均按固定 EXE 指纹核对。完整研究输出见 `evidence/animator-class-registration-research.json` 和 `component-name-lookup-research.json`。

只读探针不持有跨回调组件指针，不返回地址，不创建 Animator。查询有 32 次、8 ms 和深度范围限制；“唯一”仅限该次扫描范围，后续接管仍须排查更深层及自定义写入组件。

## 下一阶段的具体接管流程

### 用户反馈后的只读研究（2026-10-13）

新增 `animator_metadata.h`，在已验证的当前角色 Animator 上检查名称元数据。离线复核 `0x1c0e937` 的 `UnityEngine.CoreModule.dll / UnityEngine / Behaviour` 注册及 `0x1c0e97b` 的缓存写入，固定指纹后只读缓存槽 `0x56cf1b8` 所指结构的 `+0x108`。证据见 [注册反汇编](evidence/behaviour-registration-research.json)。

[客户端采样](evidence/genshin-7.1-animator-metadata.json) 再次得到唯一 Animator（节点 20），无类型/owner 不匹配、无异常、无 setter 调用；Hook 撤销且原入口恢复。Animator 和 Behaviour 两个对象都在 `+0x28` 读到准确类名，不能使用常见 IL2CPP 类名 `+0x10` 的结构假设。扫描各类前 512 字节未找到相等的直接父类指针；候选指针表各前 8 项的有界检查未取得 `get_enabled/set_enabled` 等白名单方法名。报告保留 `parent-unconfirmed`，不宣称父类不存在或整个方法表没有这些接口，也不把候选地址当作可调用函数。

本机 EXE 中精确 ASCII `get_enabled/set_enabled/get_updateMode/set_updateMode` 搜索也无命中，见 [字符串研究](evidence/behaviour-name-research.json)。EIEM 使用已解析的 IL2CPP 方法和运行时调用，因此仅照抄 `SafeSetAnimatorEnabled(false)` 不能解决原神接口解析。后续需要从本版本类型/方法访问器的调用链核对真实布局，再验证 getter；拍照模式仍可作为独立的人工场景对照。

只读发现和返回状态已加入测试：正确名称与代码候选关联、类名未确认时拒绝深入、固定 profile 未验证时拒绝读取。Animator 构建通过 657 项核心检查与 44 项运行时检查；默认只读构建通过 657 项核心检查、35 项运行时检查、CLI 采样和未知 EXE 拒绝检查。源码、二进制和采样指纹见 [验证清单](evidence/animator-metadata-validation.json)。未执行 Animator 停用实验，未实现稳定姿态接管。

1. 先确认 `Behaviour.get_enabled/set_enabled` 的名称和语义，完成 getter 只读验证。发现的布尔函数候选尚未纳入可调用 profile。
2. 重新查找上述唯一 Animator，核对类型、路径、所属角色和 native 身份，保存原 enabled 状态与目标骨骼基线。
3. 在主线程停用目标 Animator并复读确认；保持游戏时间比例不变，运行同一两秒轨道，对比覆盖数。停止时先恢复姿态，再恢复组件原状态并复读。
4. 若仍有覆盖，只观察该角色的 IK、注视、移动根骨、程序化动画/次级物理写入顺序，再逐个接管经验证的写入者。不能把 EIEM 的 FinalIK 类直接当作原神的组件清单。
5. 拍照模式作为独立对照：正常场景、拍照模式、关闭待机选项分别测量。当前未做该对照，不假定进入拍照模式就会停止骨骼回写。

不会全局拦截所有 Transform setter：动画 job 可能直接写姿态缓冲而不经过公开 setter，且全局拦截会影响 MMD 自己及其他角色。组件级接管和经验证的执行顺序应同时考虑。

## 构建与测试

```powershell
# 暂停时间实验；默认关闭
.\scripts\build.ps1 -RuntimeProbe -SingleTrackPlayback -FreezeTime -Test -OutputDirectory build-freeze-next
python .\scripts\generate-single-track-fixture.py .\build-freeze-next\single-track.vmd

# Animator 只读观察；与姿态实验互斥
.\scripts\build.ps1 -RuntimeProbe -AnimatorProbe -Test -OutputDirectory build-animator-next
```

CMake 对应选项为 `GIMMD_FREEZE_TIME` 和 `GIMMD_ANIMATOR_PROBE`，均默认 OFF，依赖/互斥条件与脚本一致。已加载的目录不能原地覆盖，应使用新输出目录。

暂停实验加载版本通过 657 项核心检查和 65 项运行时检查；Animator 加载版本通过 657 项核心检查和 40 项运行时检查。测试包含非默认时间比例恢复、已暂停时拒绝接管、部分写入异常、外部时间比例覆盖、时间恢复失败重试、骨骼身份丢失时优先解冻、私有消息过滤，以及 Animator 类型、所属对象和歧义检查。最终整合源码另做只读、普通单轨及暂停实验回归；构建和证据指纹见 `evidence/animation-ownership-validation.json`。
