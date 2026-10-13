# 第三阶段：角色层级、旋转与绑定预览

日期：2026-10-12，Asia/Shanghai。目标仍为用户指定的原神国服 7.1.0，PID 9620。接续 [相机观察记录](CAMERA_OBSERVER_RESULTS.md)。

## 已验证的结果

正式客户端中完成三次有界观察：

| 观察 | 结果 |
|---|---|
| 256 节点原型 | 成功读取旅行者骨架，触及数量上限，明确标记为 partial |
| 完整层级 | 465 节点全部处理完成，无数量、深度或时间截断 |
| 层级与旋转 | 465 节点四元数全部可读，464 条非根父子关系全部通过旋转组合校验 |

三次均未调用姿态或相机 setter，无引擎异常，自己的临时 Hook 均停用成功、入口原有字节恢复。最后一次观察有 96 次 FOV 回调。

[完整层级与旋转证据](evidence/genshin-7.1-hierarchy-pose.json) 中，所有节点的名称候选结果一致、所属 GameObject 名称一致、父子关系检查通过。根节点为 `AvatarRoot`，其下有两套同名 `Avatar_Boy_Sword_PlayerBoy(Clone)`：

| 快照节点 ID | 上游 active getter | 含自身的子树节点数 | 唯一 MMD 映射 |
|---|---|---:|---:|
| 1 | true | 273 | 52 |
| 2 | false | 191 | 52 |

因此可以给出节点 1 作为当前快照的绑定候选；active 状态尚不能独立证明最终渲染身份。ID 只是快照索引，不能跨切场景、换角色或下一次采样缓存成对象句柄。

## 接口消歧依据

`scripts/inspect-gi-functions.py` 仅对 EXE 文件做 x64 反汇编，可按 RVA、展开信息范围及直接分支候选检查调用关系。展开信息的范围可能只是函数片段；分支字节候选也可能落在指令中，必须复核指令边界，不能直接生成可调用地址。

本轮核对并固定了以下入口。运行前要求完整 EXE SHA-256 一致，且每个新入口的前 32 字节与本地证据精确一致；仍沿用原有游戏线程与临时 Hook 检查。

| 入口 | RVA | 依据及实测 |
|---|---|---|
| 名称候选 A / B | `0x14389f0` / `0x1438b70` | 两者有效对象路径均经 native 对象的同一虚函数，尾调到已验证的 FindString 转换入口；两者均在已知根节点返回 AvatarRoot，465 节点结果均一致 |
| 子节点数量 | `0x1437d10` | native Transform 的 `+0x90` 计数，与 GetChild 的上限检查一致 |
| 按索引取子节点 | `0x1438070` | 检查索引非负、低于 `+0x90` 计数，从 `+0x80` 数组读取并转换为托管对象 |
| 父节点 | `0x14374b0` | 读取 native Transform 的 `+0xa0` 父指针并转换；所有非根父关系实测通过 |
| GameObject → Transform helper | `0x1452440` | 由已验证 Component.get_transform 包装器调用，显式输出指针与 native 所属 GameObject；根节点转换后名称一致 |
| Component → GameObject | `0x1453c60` | 已验证组件包装器簇的相邻入口，完整复核 unwrap、所属对象字段及托管转换；所有节点 owner 名称一致 |
| GameObject.get_active | `0x1452eb0` | Fufu 的 GetActive 特征唯一文件匹配，按照上游 E8 相对位移解析；实测区分两套角色 |
| 世界旋转候选 | `0x19cb66a0` | 转调 `0x1436e80` → `0x15c6360`，通过层级旋转求值 |
| 局部旋转候选 | `0x19cb6840` | 转调 `0x1436ed0` → `0x15c6390`，读取节点局部旋转存储 |

最后两个入口进一步用运行时 `parentWorld * local ≈ world` 验证，接受四元数正负等价；464 条关系全部通过，且上臂等节点的世界与局部旋转明显不同。这支持本构建、该场景中的接口语义，不代表已验证 setter 或所有对象生命周期。

## 有界观察器

实现位于 `src/windows/hierarchy_observer.h`，由已有 `runtime_probe.cpp` 在确认的 Unity 窗口线程调用。

- 最多 1024 节点、深度 24、单节点 128 个子节点，名称最多 95 个 UTF-16 code unit。
- 每个节点仅读取一次 native 身份用于去重，避免逐对跨进程读取造成超时。父子关系仍逐项验证。
- 遍历检查 12ms 时间预算；单个引擎函数无法被中途抢占，因此这是软预算。超限返回 partial，后续绑定工具拒绝使用不完整结果。
- 采样结束清除内部对象地址与身份，不在下一帧重新使用；报告仅包含节点索引、名称和数值。
- 引擎读取仍由外层 SEH 包围，异常后停止观察；JSON 名称逐 code unit 转义，避免引号、控制字符或中文破坏报告。

## MMD 绑定预览

[绑定预览 JSON](evidence/genshin-7.1-bind-preview.json) 由 `scripts/build-bind-preview.py` 从完整验证快照离线生成。

52 项映射包括躯干、头颈、肩臂、腿脚及左右各 15 个手指关节。两个角色子树分别查找，避免同名骨骼跨实例混合；每项给出 MMD 名称、CP932 十六进制名称、目标名称、唯一性和旋转关系验证结果。

这仍是候选映射：躯干关节分配需要校准，当前动画姿态不是 bind pose，骨骼轴与长度比例尚未测定。`全ての親`、`センター`、`グルーブ`、`腰` 保留为虚拟通道；足部/脚尖 IK、扭转分配与表情未实现。`writeEnabled=false`，没有把预览宣称为动作重定向完成。

## 构建、测试及复现

PowerShell 构建已通过 `/W4 /WX`，657 项核心检查、32 项观察器检查通过。另有 7 项 Python 绑定测试，覆盖实例隔离、歧义 active 状态、同名骨骼、多/缺失 ID、环、partial 拒绝及 CP932 名称。

```powershell
.\scripts\build.ps1 -RuntimeProbe -Test -OutputDirectory build-hierarchy-next
python -m unittest discover -s tests -p test_bind_preview.py -v
python scripts/build-bind-preview.py docs/evidence/genshin-7.1-hierarchy-pose.json build-hierarchy-next/bind-preview.json
```

离线反汇编和 profile 生成脚本需要 Python 的 capstone；运行和 C++ 构建不依赖它。`generate-hierarchy-profile.py` 仅接受本轮固定指纹，版本改变必须重新调研，不能手动改版本字符串绕过校验。

[构建与验证摘要](evidence/hierarchy-validation.json) 保存了实测 DLL 的哈希。`build-hierarchy`、`build-hierarchy-complete`、`build-hierarchy-pose` 中已加载的 DLL 保持映射至游戏退出；后续构建使用新目录。

## 下一步

P0 已完成加载、游戏线程读取、角色层级访问、旋转 getter 消歧和候选骨骼映射。仍需验证唯一活动角色的渲染身份、局部旋转 setter 和异常时恢复，再开展同一回调内的单骨微小旋转/还原实验。之后才接入 VMD 连续驱动、绑定姿态校准和停止恢复。
