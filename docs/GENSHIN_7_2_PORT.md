# 国服 7.2 适配记录

2026-11-23 更新：已实机修通男主全身播放及 GUI 持续会话；恢复 v23 已载入模型复制、演员 FK、根位移和腰部辅助骨。新增 SDK 独立资源加载，恢复无需预览的直接加载，并修复退出角色预览后副本变紫的问题。纳西妲与沃雅妮莎已通过新加载器的动作生命周期检查；用户确认纳西妲直接加载、预览复制后返回大世界的颜色和贴图均正常。BGM 暂停仍待迁移，不能称为所有功能均已适配。

## 当前交付

- GUI 默认构建：`build-studio-7.2-face`；面部表情为可选实验功能，默认关闭。
- 当前角色播放：在大世界保持目标角色出场，在播放页选择动作后播放。男主保留原标定；其他角色使用自身 Avatar 参考姿态。纳西妲已实机通过，其他角色待扩展验证。
- 其他角色：开启会话，选择目录中的角色，点击“直接加载并创建”，再用演员槽位的动作按钮播放，无需先打开预览。当前角色和 Ambor 仍使用“复制已载入模型”。
- “复制已载入模型”继续支持队伍和缓存模型；识别到预览模型 `_Manekin` 时，先通过 SDK 获取独立资源引用，再创建副本。清空演员保留引用，结束会话时先清理副本再归还引用。本会话最多执行 16 次资源加载，达到上限后需重新开启会话。
- 数值测试通过不等同于所有舞蹈视觉质量、任意角色、跨场景稳定性或相机最终渲染时序已确认。

## 本轮实测证据

汇总：`evidence/genshin-7.2-live-summary.json`。所有列出的会话均完成钩子停用、入口字节恢复、消息钩子移除，engineException=0。

| 项目 | 会话 | 结果 |
| --- | --- | --- |
| 男主真实动作 | 20261123-225247-a7d87816 | 52 骨骼、暂停、跳转、继续、停止恢复通过 |
| GUI 持续会话与手动相机 | 20261123-230110-9f24889f | 动作恢复、位置/旋转/FOV 调整与恢复通过；286 次相机写入，回读与恢复误差为 0 |
| 男主演员 | 20261123-225558-d608f7af | 根位移、腰辅助骨、暂停/恢复、播放中清理通过 |
| 诺艾尔演员 | 20261123-225714-ff91a426 | 同上，证明演员通道不是男主固定参考姿态通道 |
| 纳西妲缓存演员 | 20261123-230152-3700203c | 缓存搜索、52 轨道、根位移和生命周期检查通过 |
| 纳西妲 SDK 直接加载 | 20261123-233639-54618643 | 6 个蒙皮、9 个材质槽有效，用户确认颜色与贴图正常 |
| 纳西妲 SDK 动作 | 20261123-233748-af259cae | 根位移、腰辅助骨、动作生命周期、清理与句柄归还通过 |
| 沃雅妮莎 SDK 动作 | 20261123-233753-47e60c2d | 无需角色预览，根位移、动作生命周期、清理与句柄归还通过 |
| 纳西妲预览复制修复 | 20261123-234038-0aed67ec | 返回大世界后 9 个材质槽有效，用户确认颜色与贴图正常，退出后独立资源引用归零 |
| 最终默认构建 | 20261123-234616-346379c2 | 纳西妲直接加载、根位移、腰辅助骨、完整动作生命周期通过；断言退出后句柄归零、无异常和遗留副本 |

只读观察器 v3：完整读取男主 139 条 Avatar 参考骨骼、121 个节点关联及蒙皮绑定矩阵。

## MMD 面部表情（2026-11-24）

播放页新增“播放 VMD 内的面部表情”开关，保存在工程的 `faceEnabled` 字段中；当前角色与载入演员共用映射。默认关闭，示例工程 `projects/7.2-face-moonride.gimmd.json` 已开启。旧工程仍保留原构建路径，需选择 `build-studio-7.2-face` 才能开启表情。

现支持眨眼、笑眼、左右单眼眨眼、`あ` 张嘴、近似 `お` 嘴形、微笑/猫嘴以及部分眉形。读取动作 VMD 自带的 morph 轨道；独立表情 VMD 合并、`い/う/え` 口型和目线骨骼尚未接入。缺少匹配通道的表情跳过，不能保证不同模型语义完全一致。

初版实测出现卡顿，用户退出测试后确认游戏恢复。逐帧遍历模型的开销较高，优化为按已验证的节点路径重新取得组件，仅在权重变化或周期检查时处理。之后发现 `ElementManager.LateTick` 在 `tickable=false` 时仍调用 `BaseEmoAnimation.Apply`：暂停时间不能阻止旧权重写回。现通过 SDK 的 `CurveBinding.BanBindApply` 暂停 MMD 所控制的脸部通道写入，逐项保存并恢复原标志。每次访问均从当前模型重新验证管理器、列表成员、渲染器和通道；不会销毁当前角色的组件。

同一《Moonride》对照：修复前会话 `20261124-114901-29806321` 记录 508 次外部权重覆盖；修复后 `20261124-115241-c3716a8f` 覆盖为 0，4 个表情轨道匹配、363 次权重写入、32 个权重和 32 个绑定标志全部恢复，无待清理状态。用户明确确认“不再抽搐”。证据为 `evidence/genshin-7.2-face-moonride-isolated.json` 及其引用的原始探测报告。停止还通过骨骼参考姿态、辅助驱动、Animator、相机与钩子恢复检查。

构建通过 723 项核心和 266 项模拟运行时检查；44 项 Python 测试通过。载入演员会话 `20261124-115402-510bd63a` 也通过《Moonride》播放、暂停、继续、末帧保持、恢复、播放中清理和资源归还；最终报告为第二次短回放的统计，不能用其写入数代表第一遍完整动作。汇总见 `evidence/genshin-7.2-face-summary.json`。另将一次性的 Animator 方法枚举预算从 20ms 调至 48ms，避免负载较高时误报方法扫描不完整；不放宽指纹、方法名或对象身份验证。

## 播放前相机归位（2026-11-24）

`build-studio-7.2-camera-front` 在播放页增加“播放前相机归位（正对角色，清零手动偏移）”，默认勾选并保存在工程中。每次开始新的当前角色动作时，读取角色模型的世界朝向及头部高度，将镜头放在角色前方并朝向身体中心；手动模式归位视角为 45°。距离、高度、角度及 FOV 可继续调节。暂停后继续和动作循环不会重新归位。使用相机 VMD 时，其相对轨迹从归位后的视角出发。

归位目标与用于恢复的原视角分别保存。原有演员跟随会解除；关闭/恢复相机或退出会话仍恢复原位置、旋转与 FOV。角色身份或相机不可用时不开始动作，等待最多 5 秒；等待期间可取消。该功能不进行地形/墙体避障。

实机会话 `20261124-110830-5eda2300`：自动启动相机、正面归位后播放、暂停/跳转/继续、手动参数调整、退出恢复均通过。用户反馈“已正对角色。别的可以手动调节”。镜头位置/旋转及 FOV 恢复误差为 0，无待清理状态；默认骨骼姿态与辅助驱动恢复也通过。证据见 `evidence/genshin-7.2-front-camera.json` 与 `evidence/genshin-7.2-front-camera-summary.json`。723 项核心、263 项模拟运行时、43 项 Python 检查通过。

## 播放前默认骨骼姿态（2026-11-24）

默认构建 `build-studio-7.2-reference` 在 Animator 与辅助物理驱动暂停后，按当前角色自身 Avatar 参考数据初始化骨骼的局部位置与旋转，再执行 VMD。覆盖有有效参考对应关系的骨骼，包括没有 VMD 轨道的辅助骨骼；不移动角色实体/模型在世界中的位置，不修改模型缩放。没有参考数据的节点不写入。

初始化前先读取全部恢复快照，再进行默认姿态写入；这些快照与播放基准分离。停止时先恢复 FK、根位移等通道，再恢复初始化前的骨骼快照，最后归还辅助驱动及 Animator。写入失败走同一恢复流程，身份和路径核对继续保留。

纳西妲实机会话 `20261124-105826-be80195e`：133 个骨骼初始化、133 个骨骼恢复，暂停/跳转/继续/停止通过，辅助驱动恢复且无遗留清理状态。记录见 `evidence/genshin-7.2-reference-pose.json` 和 `evidence/genshin-7.2-reference-pose-summary.json`。723 项核心、258 项模拟运行时检查通过；新增实际路径绑定、不同初始姿态得到相同默认姿态、初始化前快照恢复检查。

## 当前角色披风抽搐修复（2026-11-24）

载入副本会删除 DynamicBoneArray 等辅助驱动，当前角色原本仅暂停 Animator，仍保留布料/头发物理。新增辅助驱动暂停与恢复流程：只处理模型树内的 DynamicBoneArray、DynamicBoneRigidArray、MonoAnimationIKEventHelper，保存 enabled 原值并在播放前关闭，身份与所属对象重新检查，停止后恢复。不会删除本体组件。首次关闭失败阻止骨骼写入，恢复失败保留清理状态并保持 Animator 暂停；恢复成功后才归还 Animator。

纳西妲实测命中 4 个 DynamicBoneArray 和 1 个 IK 辅助组件，均从 enabled=true 暂停并恢复。循环播放会话 `20261124-104827-904894dd` 用户确认“已不再抽搐”。实测报告见 `evidence/genshin-7.2-drivers-player.json`、`evidence/genshin-7.2-drivers-visual.json` 和 `evidence/genshin-7.2-drivers-summary.json`。最终构建 `build-studio-7.2-drivers` 通过 723 项核心、254 项模拟运行时检查；包括暂停失败回滚、恢复失败保持接管及重试恢复。视觉结论只覆盖本轮纳西妲测试。

## 当前角色通道（2026-11-24）

用户反馈当前出场的其他角色仍绑定失败。失败会话 `20261123-235814-aacc2ae1` 的当前模型为 `Avatar_Loli_Catalyst_Nahida`，原因为 `full-body-reference-mismatch`：旧主角通道固定核对男主模型与完整参考骨架，而载入演员按角色姿态绑定。

新构建 `build-studio-7.2-characters` 让当前角色复用演员的 FK 计算，通过角色自身 Avatar 骨骼对应关系读取局部参考旋转；继续验证骨骼唯一性、对象身份、四元数及参考数据有效性。停止时仍使用原来的 Animator 接管/归还和姿态恢复。腰部辅助骨也按当前角色的参考数据绑定。男主的严格参考核对和原有 IK 保留。

其他当前角色先对齐载入演员的功能范围：52 轨 FK、根位移、腰部辅助骨、暂停/跳转/继续/停止；不使用男主专用的腿/脚尖 IK、辫子与披风标定。参考骨架缺失或目标骨骼缺失时仍拒绝写入。尚未逐个验证所有角色。

纳西妲本体实机会话 `20261124-000550-eff29c91` 已通过真实 VMD 播放、暂停、跳转、继续、停止恢复；52 轨恢复，根位移和腰辅助骨各写入 55 次，辅助骨无覆盖。没有创建角色副本。初次成功记录见 `evidence/genshin-7.2-nahida-player.json`。最终构建在新进程 14720 上复测：纳西妲本体会话 `20261124-103953-b1ef0573` 与载入演员回归会话 `20261124-103937-d5f1841b` 均通过，退出后姿态、Animator 和演员资源已恢复。最终记录见 `evidence/genshin-7.2-current-player-final.json` 与 `evidence/genshin-7.2-characters-summary.json`。723 项核心、251 项模拟运行时和 42 项 Python 检查通过。

## 演员拍摄控制

`build-studio-7.2-shooting` 新增演员页按钮：隐藏/显示当前角色、移到所选槽位、跟随所选槽位、恢复相机。相机使用演员槽位 1–3；先在相机页启用手动相机。定位保留镜头角度，以两名角色的骨盆位置差平移镜头；跟随持续更新演员位移，可叠加现有手动参数。当前不自动绕角色旋转，也不与相机 VMD 同时绑定。

隐藏只修改当前角色模型的 MeshRenderer / SkinnedMeshRenderer.enabled，保存各个组件原值；显示和退出会话时按身份重新查找并恢复，不关闭角色实体或骨骼。恢复失败保留清理状态。清空演员解除跟随，结束会话恢复相机。切换角色前建议先显示当前角色；跨场景销毁后的恢复尚未实测。

实机会话 `20261123-235628-c7fa3297` 通过隐藏/显示、无手动相机时拒绝绑定、定位、跟随、空槽位拒绝、解除绑定、清空演员自动释放相机、保持隐藏时退出并恢复。最终隐藏计数、资源引用为零，相机恢复且无引擎异常。数值检查通过，最终画面构图尚待用户体验。证据见 `evidence/genshin-7.2-actor-shooting.json`。723 项核心检查、249 项模拟运行时检查通过；新增跟随位移、位移不累积、单次定位不继续跟随、恢复原镜头的模拟检查。

## 预览副本变紫与直接加载

旧复制路径在预览中检查正常，返回大世界后出现 4 个失效材质引用，而网格仍存在。源模型和副本的材质复制检查均为零差异，证据指向预览退出时共享资源被卸载。诊断见 `evidence/genshin-7.2-preview-material-lifetime.json`，修复实测汇总见 `evidence/genshin-7.2-sdk-prefabs.json`。

新路径使用 SDK 的 `CommonMiscs.LoadIndependentObject(string, ref uint)`（RVA `0x6EBB750`，调用其泛型包装入口），由游戏自身按资源路径加载依赖。纳西妲路径为 `ART/Char/Avatar/Avatar_Loli_Catalyst_Nahida/Avatar_Loli_Catalyst_Nahida_Manekin`。旧目录仅提供模型名称，没有复用旧版本的资源哈希或包 ID。创建前校验类型和模型名称，在非激活父节点下复制并剥离行为组件。

会话跨回调只保留独立资源句柄，不缓存托管对象指针。结束时先删除全部副本，再调用 `CommonMiscs.DismissExternal(uint, bool)`（`0x6EB81E0`），使用 `TryGetAssetByHandle`（`0x6EBB080`）确认句柄失效。异常或未释放引用会保持待清理状态。上述入口均有 EXE 指纹和入口字节检查。

验证范围是当前角色目录及列出的实测角色；尚未逐个验证所有角色，也未把“当前进程无需预览”宣称为每个角色都通过全新进程的首次加载测试。

## 首次测试故障及修复

- 非演员构建曾回退到 1024 节点上限，队伍五个模型导致扫描截断。容量恢复为 4096；单角色观察器保留所有队伍根节点用于歧义检查，跳过未出场角色的子树，避免无关骨骼消耗一次回调预算。
- 类名字段仍用 7.1 的 +0x28，7.2 实测为 +0x70；方法表为 +0x98，数量为 +0xC2；方法 invoker 指针在 +0x10，而非旧 +0x20。统一加入 profile 常量，修复 Avatar、Renderer、Mesh、Transform 和演员组件的读取。
- 初次只迁移旧主角播放通道而关闭 LocalActors，遗漏了 v23 已有演员能力。本轮恢复 LocalActors 和 actorMotion；原资源代码保留，但未核对的冷加载入口继续拒绝编译启用。
- 校正实机测试脚本：持续会话须明确发送 camera-on，退出恢复后 acquired=false 是预期状态；演员状态文件短暂重写时，复用已确认快照，避免第二次读取返回 None。

下面保留首次迁移的输入和构建说明；状态以本节实测更新为准。

同日首次 GUI 实测：已结束会话 `20261123-223114-222aaeb8` 和 `20261123-223143-b5a3a50f` 均通过 EXE 指纹和入口检查，但 AvatarRoot 包含五个角色，扫描达到 1024 节点上限，报告 `hierarchy-partial` / `incomplete-hierarchy-refused`，没有执行姿态写入。当前出场角色为纳西妲。已将单角色构建节点容量恢复到 4096，并通过 723 项核心检查和 231 项模拟运行时检查；新容量尚待实机复测。全身 FK 仍严格使用男主 `Avatar_Boy_Sword_PlayerBoy` 的参考标定，纳西妲与女主未完成标定，不能仅靠扩大节点容量支持。

## 固定输入

- Dump：`C:\Users\L\Downloads\端游genshen7,2.cs`，文件头标记 Genshin Impact CN 7.2.0。
- EXE：`E:\Game\Genshin Impact Game\YuanShen.exe`。
- EXE SHA-256：`8B014F64B5DC12FBC476E0C362B053ABFDD7A1378CD282E4AD81057621DBD6B5`。
- Dump SHA-256、迁移前地址、候选及新地址见 `evidence/genshin-7.2-hierarchy-relocation.json` 和 `evidence/genshin-7.2-core-profile.json`。

仅匹配这一份 EXE；更新后的同版本客户端也需要重新核对指纹。

## 改动

重新扫描基础定位特征，结合 dump 中的 Unity 方法及反汇编调用链消除骨骼、相机、Animator、Avatar、Renderer、UI Text 等入口的歧义，生成新 RVA 和 32 字节运行时检查。

IL2CPP 方法表从 class+0x58 移至 +0x98，方法数量从 +0xC0 移至 +0xC2；方法名访问器现在从间接结构 +8 读取编码名字。采用新版本自身访问器，不在项目中复制解码常量。Animator/Behaviour 类缓存分别迁至 0x57B3CD8 / 0x57B4C18。所有核心方法枚举共用 profile 中的布局常量。

GUI 默认使用 `build-studio-7.2-face`。已有工程若保存了旧构建目录，需在 GUI 中选择新目录。新构建保留完整文件指纹、入口字节、对象身份、运行时方法名检查及停止恢复逻辑。

## 构建

```powershell
.\scripts\build-7.2.ps1
.\scripts\build-7.2.ps1 -ReadOnly
```

第一条生成主角 FK、根位移、腿/脚尖 IK、时间轴、相机、已载入模型复制和 SDK 直接加载演员版本；第二条生成用于实机首次检查的只读骨架/Animator/参考姿态观察器。

离线重新生成需要 Python 和 capstone，依次运行：

```powershell
.\scripts\inspect-pe-patterns.ps1 -Executable 'E:\Game\Genshin Impact Game\YuanShen.exe' -Output .\docs\evidence\genshin-7.2-patterns.json
python scripts/generate-gi-profile.py docs/evidence/genshin-7.2-patterns.json references/fufu/FufuLauncher.UnlockerIsland__Patterns__Patterns.h src/generated/gi_profile.h
python scripts/generate-gi72-profile.py 'E:\Game\Genshin Impact Game\YuanShen.exe' 'C:\Users\L\Downloads\端游genshen7,2.cs'
python scripts/generate-gi72-actor-profile.py 'E:\Game\Genshin Impact Game\YuanShen.exe'
```

`generate-hierarchy-profile.py`、`generate-actor-profile.py` 和 `generate-resource-profile.py` 是旧 7.1 研究脚本，不用于生成 7.2。`port-gi-profile.py` 是离线候选调查工具，其输出本身不代表接口已验证。

## 验证及限制

- 单角色构建：723 项核心检查、231 项模拟运行时检查通过，包含不匹配 EXE 拒绝测试。
- 只读观察器构建：723 项核心检查、82 项模拟运行时检查通过。
- 带演员的最终构建：723 项核心、245 项模拟运行时检查通过。
- Python：42 项测试通过，包括仅有眼神轨道的 VMD 在替换现有会话素材前给出明确提示。
- 当前主角播放与演员生命周期已实机验证；见上方会话记录。画面质量仍需用户确认。
- LocalActors 已恢复，`SdkPrefabs` 提供新版本直接加载。旧 ActorTemplates、ActorSurvey、ResourceTrace 仍存在旧版资源地址，7.2 profile 拒绝这些构建选项；不复用旧资源 ID。
- 新客户端的 `cri_ware_unity.dll` 指纹发生变化，BGM 暂停仍会按现有检查拒绝启用；独立 WAV 音频路径不依赖该 DLL。
- 后续：更多角色和完整动作的视觉确认、全新游戏进程中的首次加载覆盖、BGM 新 DLL ABI 核对。
