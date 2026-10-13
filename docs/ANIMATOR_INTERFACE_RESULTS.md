# 原神 Animator 接口定位与启停恢复验证

2026-10-13，原神国服 7.1.0，沿用固定 EXE SHA-256 `7F89938DA606C1281659607D464702CDDB9A09A7D4320A196630F60A811EC38E`。

**已在正式客户端确认当前角色 Animator 的 enabled 读写接口，并完成同一主线程回调中的 `true → false → true`。尚未验证跨帧停用能否阻止骨骼覆盖。**

后续已完成两秒跨回调上臂接管及普通播放对照，见 [两秒接管实测](ANIMATOR_LEASE_RESULTS.md)。以下保留本阶段的接口验证记录。

用户授权自行启动游戏后，本轮启动 `YuanShen.exe`，PID 6020。采样时已有可用角色场景。未操作登录或账号。

## 为什么上一轮没有发现方法

上一轮按直接指针搜索父类和方法名称，没有得到结果。本轮沿已确认的类型注册调用链追踪到方法查找实现 `0x511b50`：

- 方法指针表位于类的 `+0x58`，方法数量为 `+0xC0` 的 uint16。
- MethodInfo 的 `+0x00` 指向声明该方法的类，`+0x2E` 保存参数数。
- 名称通过 `0x4b2be0` 获取；它可能调用 `0x5291b0` 初始化名称缓存。`0x511c32` 开始的查找比较代码使用同一名称路径进行字符串比较。
- 父类不是直接存储的完整指针。`0x4b2330` 根据类的 `+0xB0` 相对值及全局基址返回父类，方法查找的父类遍历使用同一表示。

这说明上一轮“找不到直接父类指针”不能推导出继承关系不存在。新增反汇编工具 `--linear` 用于跨 Windows unwind 片段阅读有界代码窗口；窗口可能含相邻函数，仍须人工核对入口和语义。

证据：[类型查找链](evidence/class-resolution-linear.json)、[方法查找](evidence/class-method-search.json)、[名称与父类访问器](evidence/behaviour-method-accessors.json)。这些接口及调用链入口已加入固定 EXE 指纹与 32 字节核对。

## 按名称验证的接口

[客户端名称采样](evidence/genshin-7.1-animator-named-methods.json) 确认 Animator 的父类恰好是已命名注册的 Behaviour；检查了 Behaviour 全部 4 个方法及 Animator 前 128 个方法，没有方法 owner 不匹配。Animator 共 311 个方法，因此整体报告标记为 partial，不能称为完整方法表导出。

| 方法 | 参数数 | MethodInfo +8 入口 RVA | 已核对的 native 入口 |
|---|---:|---|---|
| Behaviour.get_enabled | 0 | `0x19c8cca0` | `0x10dee70` |
| Behaviour.set_enabled | 1 | `0x19c8ccb0` | `0x10deeb0` |
| Behaviour.get_isActiveAndEnabled | 0 | `0x19c8ccc0` | `0x1455060`，本轮未调用 |
| Animator.get_updateMode | 0 | `0x19c86940` | `0x11a18f0` |

MethodInfo `+0x20` 的另一个代码地址是运行时 invoker，例如 bool getter 的 `0x2f4a70`、bool setter 的 `0x2f3bc0`；不能与实际实例方法入口混用。getter 接收 managed component，返回 bool；setter 接收 managed component 与 bool，native 路径分别经对象转换后调用虚表 `+0x108/+0x110`。证据：[包装器与 invoker](evidence/behaviour-enabled-wrappers.json)、[native 调用](evidence/behaviour-enabled-native.json)。

只读 [状态采样](evidence/genshin-7.1-animator-state.json) 得到 `enabled=true`、`updateMode=0`，两次 enabled 读数一致。[Unity 官方枚举定义](https://github.com/Unity-Technologies/UnityCsReference/blob/master/Modules/Animation/ScriptBindings/Animator.bindings.cs) 中 0 对应 Normal，因此按标准枚举解释，本次观测不支持此前 UnscaledTime 的猜测。它也不能解释游戏为什么在 timeScale 归零后仍然运动；实际时钟和自定义更新路径仍待核对。

## 同一回调的启停恢复实验

[原始报告](evidence/genshin-7.1-animator-toggle.json)：

| 指标 | 结果 |
|---|---|
| 实验状态 | animator-toggle-restored |
| 原状态 | true |
| 停用后复读 | false |
| 恢复后复读 | true |
| setter 调用数 | 2 |
| 恢复尝试 | 1 |
| 异常 | 0 |
| 待恢复状态 | false |
| Hook 已撤销 / 入口恢复 | true / true |
| 报告时活动回调 | 0 |

目标来自本次完整骨架扫描中唯一 active Avatar 的浅层唯一 Animator，类和 GameObject 归属均验证；本轮模型节点为 32，不能沿用前一进程的节点 20。操作前保存 enabled 原值和组件/native/owner 身份；同一回调内停用、复读，然后恢复原值并复读。恢复也会在写入或读回异常后尝试，最多 2 次。每个 DLL 映像只允许消费一次写入实验；失败会报告并关闭该探针后续实验。

前两次准备因 12 ms 完整骨架扫描上限拒绝，没有 setter 调用；保留其中一份 [拒绝报告](evidence/genshin-7.1-animator-toggle-preflight.json)。第三次完整预检通过后才执行。所有尝试完成后 Hook 均撤销。没有修改 timeScale，没有写骨骼姿态。

此实验只验证接口和原状态恢复，不跨越渲染帧，没有可见冻结效果的验证，不能证明 Animator 是唯一骨骼写入者。

## 构建与后续实验

```powershell
# 只读组件与方法观察
.\scripts\build.ps1 -RuntimeProbe -AnimatorProbe -Test -OutputDirectory build-animator-readonly-next

# 同一回调启停恢复，默认关闭
.\scripts\build.ps1 -RuntimeProbe -AnimatorProbe -AnimatorToggle -Test -OutputDirectory build-animator-toggle-next
```

CMake 选项为 `GIMMD_ANIMATOR_TOGGLE`，要求 `GIMMD_ANIMATOR_PROBE`。此开关与单轨、姿态回写实验互斥。不要覆盖已加载的 DLL 输出目录。

启停版本通过 657 项核心检查、60 项运行时检查，包括按名称关联入口、父类/owner 不匹配拒绝、参数数与状态检查、原本已停用时拒绝写入、部分写入异常恢复、恢复重试与上限，以及已消费实验拒绝重复写入。最终只读版本通过 53 项运行时检查，普通单轨版本通过 55 项，均通过核心、CLI 与未知 EXE 拒绝检查。构建和证据指纹见 [验证清单](evidence/animator-interface-validation.json)。

下一阶段将该接口接入已有两秒单轨：独立保存目标 Animator 原状态，逐回调重新解析角色与组件身份，停用后比较骨骼覆盖次数；退出先恢复骨骼，再恢复 enabled 原值。应保留不依赖游戏时钟的调度和恢复路径，并验证角色切换、失去目标及中途异常。仍有覆盖时再定位该角色的其他写入者，不能把 EIEM 的 FinalIK 组件清单直接套入原神。
