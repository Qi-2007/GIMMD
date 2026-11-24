# 原神本地多角色：第一阶段调查

2026-10-15。目标仍为已固定 SHA256 的国服 7.1；本轮未启动、注入或操作游戏，也未创建第二角色。

## 参考实现的边界

`references/CAMERA_EIEM_PORT_PLAN.md` 的多人方案读取终末地已经在场的小队成员，并为每人维护独立动作状态。它不创建可见角色。`references/EIEM_ghost_rig.h` 创建辅助 Transform 骨架，不提供角色蒙皮模型。因此不能把这些实现直接当作原神角色生成器。

已有原神快照的离线摘要见 `evidence/genshin-7.1-actor-baseline.json`：一套活跃旅行者（273 节点），一套非活跃同名对象（191 节点）。同名不表示同一实例；非活跃对象不等于可启用的队友，当前没有激活它。

Unity 的一般机制可以用于确定调查方向，但不是当前游戏版本的调用合同：

- [Object.Instantiate](https://docs.unity.com/en-us/engine/6000.3/script-reference/unityengine/object/instantiate)：复制对象可能触发生命周期回调。直接克隆整个角色实体可能同时复制脚本和其他组件。
- [Unity 蒙皮组件源码](https://github.com/Unity-Technologies/UnityCsReference/blob/master/Runtime/Export/Graphics/GraphicsRenderers.bindings.cs)：蒙皮依赖 mesh、bones 和 rootBone。新演员的 bones/rootBone 必须归属于新骨架，不能误用源角色骨骼。

## 本轮新增

`-RuntimeProbe -AnimatorProbe -ActorSurvey` 是独立、只读调查构建，与播放及 setter 实验编译选项互斥。其层级上限为 4096 节点，遍历软预算 48ms；命名元数据调查另有 24ms 软预算，最多 128 条候选。引擎单次调用无法强制中断。普通播放构建保留 1024 节点和原预算。

在已确认的回调线程上：

1. 沿用已有完整层级读取，保留所有根角色及其 active 状态。
2. 使用已校验元数据访问器，读取 Transform、GameObject、蒙皮组件及其基类的方法表。
3. 记录 Instantiate/Clone、Destroy、SetActive、AddComponent、SetParent、mesh/bones/materials 等候选名称、参数数量和代码 RVA。**不调用这些候选**；没有验证参数类型、重载、返回值和 native ABI。
4. 对 Body/Face/Hair 节点只读查询 SkinnedMeshRenderer 并检查所属 GameObject。尚不是完整渲染组件清单。
5. 输出完整 `.probe.json` 和按实例分组的 `actors.json`。后者明确标注 renderingIdentityVerified=false、spawnEnabled=false。

调查失败或截断时保留 partial 状态，不把结果升级为可生成模型。角色指针仅在回调中使用，不加入跨帧演员句柄。

## 采样入口

现成构建：`build-actor-survey/survey-actors.cmd`。

先退出现有 GIMMD 注入会话，保持游戏运行并进入角色可见场景，再运行入口。它不启动游戏；要求恰好一个 YuanShen 进程。采样自动结束，结果在 `sessions/actor-survey-时间/`。与既有观察器一样，停止 Hook 不代表 DLL 映射卸载。

普通单人场景可先确定创建/销毁方法候选。之后可在真实多人场景重复采样，比较 AvatarRoot 下的角色和模型结构；当前调查不写入任何玩家骨骼，也不读取/伪造联机消息。调查可能因软预算或缺失元数据返回 partial，不应反复自动注入尝试。

## 后续实现顺序

1. 用新报告候选 RVA 离线核对完整函数、参数类型、托管/原生桥接和销毁机制，生成本版本的精确字节 profile。
2. 先实现一个本地可见演员的创建和销毁闭环。优先复制或重建模型与骨架层，明确保留的渲染组件及材质依赖；不注册为联机角色，不复用其他玩家实体，不直接启用不明非活跃对象。切场景和中途创建失败均销毁自建对象。
3. 将当前全局 Animator 租约、骨骼映射、根位移、IK、披风与辫子状态拆成每演员一份；新增对象身份和 generation 检查。共享导演时钟、相机、音频，保留每个 VMD 的队形。
4. 接 GUI 的演员列表、各自动作/位置/旋转、单独移除；播放结束定住与恢复语义沿用动态会话。

上述是调查阶段的状态。下述实验版新增了模型创建和清理代码；现有 multi-track 仍指单角色多骨骼，多演员分别播放尚未实现。

## 验证

调查版：723 项核心检查、65 项运行时替身检查、4 项调查摘要测试通过。替身测试不执行实际引擎元数据调查；候选读取和角色生成均没有新增实机验证。原层级容量测试改为随 MaxNodes 生成越界数据，并修复只读构建误跑播放取消测试的问题。

## 2026-10-15：本地可见模型生成实验版

用户采样已读取并归档至 `evidence/genshin-7.1-actor-survey.json` 和 `evidence/genshin-7.1-actor-survey-summary.json`。完整快照包含非活跃旅行者、非活跃安柏和活跃旅行者；读取了六个 Body/Face 蒙皮组件。创建相关命名方法调查完成，无新增角色写入。候选 RVA 经本机 EXE 离线反汇编核对后，由 `scripts/generate-actor-profile.py` 固定字节及完整文件指纹。

`src/windows/local_actors.h` 在明确开启 `-LocalActors` 的播放构建中实现：

- 选择当前角色模型，或按 ASCII 模型名称筛选已经载入的模型。模型必须位于角色的 OffsetDummy 下，同时具有 Body 和 Bip001 子节点。
- `Internal_CloneSingleWithParent(GameObject, inactiveTransform, false)` 将模型副本建立在非活跃角色节点下，要求克隆结果处于非激活状态；外层实体、Collider/Trigger/HitBox 结构不作为克隆根。
- 在自己的非激活副本中，检查所有蒙皮骨骼及非空 rootBone 属于副本；保留 Transform、SkinnedMeshRenderer、MeshRenderer、MeshFilter 和停用的 Animator，移除其他组件。无法确认所属关系或删除结果时回滚整个副本。此精简策略是否仍满足原神材质显示依赖，尚需实机验证。
- 重命名为 GIMMDActor_进程号_槽位，移到 AvatarRoot 下，按当前角色世界位置和朝向在右侧间隔 2 米放置，再启用。最多三个额外副本；当前只提供静态模型，不自动贴合地形。
- 主线程创建、检查和销毁；文件工作线程仅接收命令、发布状态。退出、移除和创建失败分别执行清理；删除前同时检查 native 身份与自有名称。非激活副本通过父节点枚举寻找，避免 GameObject.Find 无法找到非激活对象的问题。清理未完成则保留现有清理 Hook。无法回滚的无路径副本标记 orphanedObject 并要求重启游戏，不能宣称已清理。
- 当前角色 MMD 活动时拒绝生成，先恢复游戏动画。移除演员和恢复当前角色动画互不替代；退出会话清理自建演员。

GUI 新增“本地演员”页，包含模型来源、生成和全部移除。入口 `build-studio-actors/test-actors.cmd`；构建目录选择 `build-studio-actors`，启动新动态会话后使用。会话内 `actors.status.json` 记录状态、各槽位节点数、蒙皮数量和删除组件数量。`actor-created` 表示接口调用、归属及位置回读通过，不是已确认画面显示。

验证：723 项核心检查、217 项运行时替身检查、25 项 GUI/控制测试通过。新增清理替身检查覆盖活跃/非活跃副本删除、来源保留及地址复用不误删。**尚未在游戏中执行本版克隆，不能确认可见性、材质效果、创建耗时或销毁稳定性。** 未将任何来源游戏对象激活为额外联机成员，也没有联机角色注册实现。

### 首次用户实机尝试与 v2

用户首次尝试返回 `clone-component-strip-refused`，没有成功演员槽位，exception=0、orphanedObject=false，状态归档为 `evidence/genshin-7.1-actor-first-attempt.json`。依照代码路径，可确认克隆、非激活检查、重命名回读及副本树遍历已经通过；失败发生在组件处理阶段。旧报告没有记录具体失败分支，不能据此断言是骨骼引用、组件封装还是销毁回读的问题；回滚未报告失败，也不是完整的实机生命周期成功证据。

v2 修正了一个可独立确定的误判：有效的零长度蒙皮骨骼数组不包含外部骨骼引用，应允许辅助蒙皮使用。非空 rootBone 和各加权骨仍必须属于克隆树。新增四项替身检查验证这两类情况。

同时保存失败节点、组件名称、具体 detail、骨骼数量和回滚前的节点/蒙皮/组件删除计数。GUI 在组件检查失败时展示具体 detail。v2 入口 `build-studio-actors-v2/test-actors.cmd`，必须开始新会话；不会热换用户当前正在加载的 DLL。验证为 723 核心检查、221 运行时替身检查、25 GUI/控制测试通过。此次首发失败的根因以及 v2 的可见性仍待新实机结果确定。

### v3：粒子组件删除与销毁后缓存

用户 v2 实机日志明确为 Smoke / ParticleSystemRenderer / component-destroy-readback-failed；231 节点、6 蒙皮、24 已处理组件，无引擎异常或 orphan 报告。证据：`evidence/genshin-7.1-actor-particle-failure.json`。它证明旧的缓存指针清零条件未满足，尚不能单靠此日志区分依赖阻止删除与删除后的包装器缓存。

v3 在任何删除之前快照组件名和 native 身份；粒子渲染器放到第二遍，先处理 ParticleSystem 等其他组件。每次使用旧组件包装器之前，从所属 GameObject 的实时组件表确认它仍存在。删除之后亦以组件表中不存在为成功条件，不再解引用已经销毁的组件来读取缓存指针。组件表读取失败仍拒绝，不等价于删除成功。

新增三项替身检查覆盖现存组件、移除后缓存地址仍存在、无效组件列表拒绝。v3 构建检查为 723 核心、224 运行时替身、25 GUI/控制测试；入口 `build-studio-actors-v3/test-actors.cmd`。实际显示仍待用户实机验证。

### 未入队角色资源加载（2026-10-15，进行中）

已离线核对命名类型查找链：`14cfa10 -> 14c6760 -> 4b2210`。
参数为输出类型指针、程序集文件名、命名空间、类名；返回输出地址。
现有 Animator 注册点传入 `UnityEngine.AnimationModule.dll / UnityEngine / Animator`。
提供器先清空输出，按程序集查找后调用类型查找；未命中返回空值。

新增资源方法采样到 ActorSurvey：按版本入口字节核验后，在既有主线程回调查询
Resources、AssetBundle、AssetBundleRequest 及三个游戏资源管理候选类型。
输出 `resourceLookupStatus`、`resourceClasses` 和候选方法 RVA/参数数量。
这些方法地址仅作为证据，不作为加载调用目标；未调用 Load/LoadAsset 或更改资源生命周期。
游戏程序集候选名称与命名空间尚未验证，未命中不能证明不存在资源管理器。

构建：`build-resource-survey/sample-resources.cmd`。需用户进入实际场景后运行。
验证：723 核心检查、65 合成宿主检查通过；尚未实机采样。
下一步依据运行时方法表核对枚举已加载资源包与资源名称的接口，再定位角色预制体、
依赖资源以及游戏自己的资源块加载入口。普通 Resources.Load 入口存在不代表能够加载未入队角色。

### 第二轮资源采样结果（2026-10-15）

用户授权后续自行采样；已对正在运行的 PID 13892 执行第二轮只读查询。
报告：`sessions/actor-survey-20261015-125713-421`；精简证据保存在
`docs/evidence/genshin-7.1-resource-methods.json`。采样正常退出，入口已恢复，未调用 setter。

关键修正：该游戏将 AssetBundle 及三个请求类型注册在 UnityEngine.CoreModule.dll，
不是标准 AssetBundleModule。注册点 1c0e6ea 的三个字符串参数已离线核对，实机查询命中。
现已获取完整方法表，包括：

- LoadFromFileWithMiHoYoPath（8 参数），19cc6690 -> 145b500。
- LoadAssetByHash（3 参数），19cc67a0。
- LoadAssetInstanceCacheByHash（2 参数），19cc6800。
- FindAssetMHYHash（1 参数），19cc6930 -> 145bbf0 -> 19febc0。

该类型没有公开 GetAllLoadedAssetBundles/GetAllAssetNames 方法；不能照普通 Unity 示例枚举。
LoadFromFileWithMiHoYoPath 桥接首先调用 19d8b90 -> 19d9d50 解析路径，成功后调用
19d8ba0 加载；后者调用 19f3700 获取原生资源管理器并通过虚表 +90/+b8 查询资源。
这些调用尚未用于主动加载。需要继续确认路径解析参数、资源块索引和角色资源 hash，
再把获取的 prefab 接入已验证的静态演员复制流程。

### 第三轮：资源包和预制体枚举（2026-10-15）

已核对 Resources.FindObjectsOfTypeAll 的链路 19ccad20 -> 1441cc0 -> 14c90e0，
以及从类内嵌类型字段 +68 获取反射类型的 52f450（与 Resources.Load 包装器一致）。
所有新增入口均使用版本匹配及 32 字节比较。枚举不激活对象、不调用加载/卸载接口。

实机结果：29,027 个 AssetBundle 对象；抽取前 128 个名称，多为数字目录/hash.asb。
不能据此判断这些对象的内容都已加载。GameObject 分批扫描 80,949/80,949 个对象，
角色模型候选仍只有 PlayerBoy、AmborCostumeWic 及相应副本；Avatar_Columbina_Light
仅按名称命中，不能视为哥伦比娅模型。没有找到可直接作为新增角色来源的其他角色 prefab。
分批扫描每次 24ms 软预算，数组不跨回调保留，重新枚举；若对象总数变化则重新扫描。
这是一段时间内的扫描，并非场景原子快照。

采样：sessions/actor-survey-20261015-131616-286。
证据：docs/evidence/genshin-7.1-resource-bundles.json、genshin-7.1-resource-prefabs.json。
游戏 engineException=0、settersCalled=false、entryBytesRestored=true。
构建 build-resource-survey-v5：723 核心检查、65 合成宿主检查通过。

进一步核对路径格式化 15b9340：根路径 + `/` + 两位分类数字 + `/` + 八位数字索引 + 后缀。
这不是角色名到资源的解析器；必须先从游戏资源目录取得数字索引和资源 hash。
目前尚未主动加载未入队角色，也未向正式 Studio 发布角色加载按钮。

### 第四轮：角色配置与资源提供器候选（2026-10-15）

扩展枚举 TextAsset，分批扫描 39,049 个对象。其名称包含大量脚本类名，
不能因出现 AvatarExcelConfig 就认定取得了角色数据表；精确托管类匹配仍会命中这些对象，
说明当前包装类型不能进一步区分它们的原生用途。未读取或导出这些对象的内容。

筛选得到 AvatarExcelConfig、AvatarExcelConfig.Utils、AssetBundleExternalResourceProvider
及多个子项、AssetBundleSettings、StreamPreloadConfig 等候选。
按 Assembly-CSharp.dll 的 MoleMole / MoleMole.Config / MoleMole.ExcelOutput
命名空间进行显式类型查询：只有 MoleMole.StreamPreloadConfig 命中（.ctor RVA ac86550）；
其他候选未命中。未命中不代表不存在，也不授权调用同名原生函数。

证据：docs/evidence/genshin-7.1-resource-text-assets.json、
genshin-7.1-resource-provider-lookup.json、genshin-7.1-resource-summary.json。
最后实机采样 sessions/actor-survey-20261015-132744-260。

补充工具：generate-resource-profile.py 可从已核验 EXE 重建入口字节；
analyze-resources.py 提取资源报告，避免输出完整场景树；survey-actors.ps1 自动保存 resources.json，
兼容没有资源字段的旧采样 DLL。新报告包含 namespace，便于区分同名类型。
最终采样构建 build-resource-survey-v10，723 核心、65 合成宿主检查通过；本轮未更改正式 Studio。

下一步需识别原生脚本描述与实际托管类型的关联，或观察正常角色资源加载时传入的索引。
当前仍缺少“角色 -> 数字目录索引、bundle/hash、依赖”的完整映射，未实现未入队角色加载。

### 已预览的未入队角色演员（2026-10-15，已实现）

当前可交付构建为 `build-studio-actors-v7/test-actors.cmd`。
保留原 AvatarRoot 来源搜索，在指定资源名没有命中时，使用已经核对的
Resources.FindObjectsOfTypeAll(GameObject) 查找预览模型。
搜索跨主线程回调分批进行，每批 12ms 软预算、总计两秒上限；命令在查找结束后才确认。
只保留搜索游标和数字计数，不跨回调保存数组或模型指针。来源确认、复制、组件检查与回滚
仍在同一主线程回调中执行。骨架验证支持模型内部嵌套的 Body/Bip001，来源名称只允许
末尾 `(Clone)`，不会放宽命令文本或自建演员名称的字符规则。

实机结果：

- 沃雅妮莎：Avatar_Girl_Catalyst_Vodyanitsa_Manekin，278 节点、16 个蒙皮、移除 41 个副本组件。
- 奥黛塔：Avatar_Girl_Sword_Odette_Manekin，241 节点、10 个蒙皮、移除 24 个副本组件。
- 两者均 actor-created，激活/位置读回通过；测试已清理，异常与残留为零。
- 奥黛塔额外检查十个 sharedMesh 均有效，骨骼归属检查通过。
- 关闭源预览后的存续检查未完成：等待用户操作超时后已自动退出和清理。
  不据此前置检查声称关闭预览后的网格、材质或最终画面已经验证。

GUI 增加 Vodyanitsa/Odette 来源、分批搜索状态和检查演员按钮；旧构建没有 actorInspection
能力时拒绝检查命令，避免永久等待不存在的确认。检查只读取副本网格及骨骼，不连续逐帧扫描。
限制：必须先由游戏载入所需模型，目前额外演员为静态，未接入各自的 MMD 状态。
已预览演员生成不等于完全未加载角色的主动资源加载。

验证：723 核心、233 合成宿主、26 Python GUI/传输检查通过。
证据：genshin-7.1-cached-vodyanitsa-created.json、genshin-7.1-cached-odette-created.json。

### 主动资源加载链路（进行中）

被动追踪已核对路径格式化 15b9340 和 LoadAssetByHash 桥接 145b710；参数、返回值与 out 参数
透传另有五个合成宿主检查。追踪可限时运行或由 resource-trace.stop 提前停止，恢复入口并保留
已禁用的 trampoline 映像，不进行物理卸载。用户第二次预览奥黛塔时捕获 152 个路径访问，
但唯一同步 Hash 记录为 EventPattern_Girl_Bow_Linnea，不能据此映射角色预制体。

已验证 FindAssetMHYHash 的语义是“资源对象的原生 instance ID -> 此资源包内的八位 Hash”，
不是按类型查找主资源。反查奥黛塔身体网格得到 bundle `868/7890bc637afe03bd.asb`、
Hash 377656923；预览根 GameObject 没有对应项，不能将 Mesh Hash 用来加载整个人物。

从原生 AssetBundle +168/+170 的 16 字节索引读到 6,449 个 ObjectInstanceCache。
缓存 +50/+68 存储八位 Hash 字符串；+88 为 MHYAssetSpawner，资源名也是 Hash。
旧 InstanceData +6c 源标识均未给出角色关联；MHY 模板首描述符 +10 的查询尚未找到角色源名称。
这里的零标识不能当作有效预制体来源。

UnityEngine.ObjectInstanceCache 在 CoreModule 中存在：
InstanceAsyncHandle 19ca1ed0 -> 145b2c0 -> 19d9820；
LoadTemplate 19ca1ef0 -> 145b320 -> 19d9660；
IsTemplateLoaded 19ca1f00 -> 145b350 -> 182af70；
Instance 19ca1f10 -> 145b380 -> 19d97c0。
尚未主动调用这些创建/加载接口；仍需确认角色 Hash 和创建时的激活、依赖及恢复条件。

资源管理脚本条目已确认是 MonoScript 描述，文本正文长度为零；未得到可用的类型映射。
超出已核对字段的启发式字符串候选已从实现移除，不能将相邻对象字符串解释为脚本类信息。
下一步核对 MHY 模板数据流 getter 和缓存描述符，尝试从模板内容恢复根模型名称。


## 2026-10-15 模板实验失败与隔离

缓存块已识别 14 个角色模板名称，但这不等于直接加载已验证。
`20261015-160857-4a3ee315` 的 Columbina 实验发生 0xc0000005，无法确认临时对象清理；用户已重启游戏。
原调用顺序在原生初始化前执行托管包装和 Transform 操作，与游戏公开 Instance 桥接的顺序不符。现已改为先初始化、后包装，并保留失败阶段信息；该修改尚未实机验证，不能据此认定异常已修复。
新增显式 `-ActorTemplates` / `GIMMD_ACTOR_TEMPLATES` 构建开关，要求 `-LocalActors`。普通构建与 GUI 默认禁止模板创建，仅使用队伍及已加载模型复制。
`build-studio-actors-v9-safe` 通过 723 项核心与 233 项合成运行时检查。
对未完成初始化的原生对象，尚未证明 DestroyImmediate 可安全且完整回收，因此本轮不再次执行模板创建。

重启后的 PID 17180 实机验证：v9-safe 复制当前旅行者成功，255 节点、7 个蒙皮网格全部有效，清理完成，exception=0、orphanedObject=false。只读采样也确认 settersCalled=false、engineException=0、entryBytesRestored=true。


## 2026-10-15 预览角色洋红色渲染问题（未解决）

用户确认截图中的洋红色副本为奥黛塔、沃雅妮莎。不能将队伍安柏或旅行者的材质验证结果视为这两个预览角色的渲染验证。
新增单回调内保存源模型共享材质、逐节点验证副本渲染器、组件清理后重绑定和逐槽回读，以及创建/清理阶段材质引用变化诊断。v11 构建通过 723 核心和 233 合成运行时检查；安柏 22 渲染器/23 材质槽创建与清理前后无引用变化，未支持“清理必然清除材质”的假设。
诊断 v12 记录移除组件类型，包括 MonoVisualEntityTool、MiHoYoLodMeshGroup、SkinnedMeshTessellationDataHolder 等；尚未确定哪些影响预览模型渲染。
在 PID 17180 未找到 Odette 源 GameObject，已请求用户保持相关角色预览打开以进行针对采样。GUI 默认仍使用 v9-safe；材质候选修复尚未确认解决洋红色。
只读元数据 Material.get_shader 与 Shader.get_isSupported 对应的代码语义存在互换嫌疑（143cee0返回bool，143d9b0返回包装对象）；未按方法名直接调用，需要进一步验证原生类型和返回类型。


## 2026-10-15 未预览角色主动加载实验

用户要求优先继续直接加载。新增仅实验构建可用的 `load-template`（仅加载资源）与 `spawn-template`（强制模板路径）；控制器按 actorTemplateLoading 拒绝普通构建，GUI 默认保持 v9-safe。
离线再次确认原生实例调用 19e0c70 的向量布局、RawTemplateClone 和 FinishTemplateClone 参数；调整后的初始化顺序尚未成功到达实机创建，因此不能认定修复了旧实验异常。新增失败阶段、exceptionRva、templateObjects 字段。
主动接口：LoadAssetInstanceCacheByHash 19cc6800→145b7c0→19d95d0；LoadTemplate 19ca1ef0→145b320→19d9660。普通 LoadFromFile(string) 19cc6630→145b450→19d8ba0→MHY 19f3dd0。全部按版本 SHA 和入口字节核验。
PID 17180 强制 PlayerBoy 与未预览 Columbina 均未找到目录指定的 Manekin 包。后续对 Columbina 调用普通 LoadFromFile(logical bundle name) 返回空，未到达缓存/模板加载。所有尝试 exception=0、orphanedObject=false、清理正常；没有成功生成未预览角色。
物理安装资源为 AssetBundles/blocks；ctable.dat 与 ctable_streaming.dat 均为 28,160,000 字节，无明文逻辑包名或包 hash 匹配。接下来需定位游戏资源目录/provider 的逻辑包→block 文件/偏移/依赖映射，而非重复尝试普通包名加载。
证据：docs/evidence/genshin-7.1-direct-template-load-attempts.json。


## 2026-10-15 莉奈娅预览加载 hook

按用户请求实现被动加载链追踪：native 文件加载 19d8ba0、内存加载 19d9030（6 参数透传）、实例缓存 19d95d0、模板加载 19d9660、同步实例 19d97c0、原始模板克隆 19e2510。保留既有格式化与按 Hash 加载 hook。每个入口使用版本 SHA 与 32 字节核验；固定 4096 事件缓冲，hook 内不写文件，捕获线程、时间、输入/输出身份、路径/偏移、调用栈 RVA。只在确认主线程读取已初始化对象名称，原始克隆不调用对象 API。
构建 build-resource-trace-v7-pipeline，通过 723 核心与 77 合成运行时检查；新增检查覆盖 64 位偏移、六参数中栈参数、返回值、输出向量、缓冲满时透传。五秒实机预检与正式采样的全部入口均恢复。
用户在追踪期间预览莉奈娅。sessions/actor-survey-20261015-172003-468：140 事件，包括 83 bundle-file、11 instance-cache、10 load-template、12 instance、24 template-clone；无截断、无异常、无活动残留、全部入口字节恢复。
角色本体缓存请求 hash=0x14DDB30E，发生在工作线程 15052；调用栈包括 0x145b7ea、0x6c1e67e、0x6de1b00、0x156766e0。后续只读采样确认该 hash 对应 Avatar_Girl_Bow_Linnea_Manekin，包 531/49b93d843d36f52a.asb，467 原生对象描述符。不能仅凭“467”将某一异步克隆事件关联到此缓存，仍需缓存/templateData 身份链证据。
原始异步克隆栈包括 0x19e0709、0x1693918、0x1693539、0x16556e8；同步路径则经过 0x19e0cd0、0x19d97f9。展示特效的缓存→模板→实例链已按相同 native identity 关联；例如 Eff_Avatar_Girl_Bow_Linnea_Show_02_Book_SkillObj 位于 66173/f1596f7683d4248d.asb，来自 blocks/00/00265660.blk 的偏移 12917634，第四参数 2701（语义尚未完全核对，未直接标作长度/flags）。
角色本体包未捕获 bundle-file 事件，可能已缓存或走另一异步入口。不能声称已经拿到本体块偏移，或可重放独立加载。下一步沿真实 AOT 调用栈和异步加载入口，补全本体资源包与依赖映射。
追踪就绪文件 resource-trace.ready.json 支持确认所有 hook 已安装；用户操作完成后 resource-trace.stop 提前结束并恢复。离线分析 scripts/analyze-preview-trace.py 校验干净结束，再按 native identity 关联链路，禁止仅按时间或相同节点数推断关联。证据 docs/evidence/genshin-7.1-linnea-preview-load-chain.json。


## 2026-10-15 异步模型链与真实资源 ID

扩展构建 build-resource-trace-v11-async-metadata 增加 MHY 包文件入口 19f3dd0、异步模板 19d9880、完成初始化 19e2d10、LoadedBundle 包装器 6c1e400、模板首次就绪 19e0b90。全部透传；就绪事件按 spawner 身份去重，元数据读取安排在确认主线程回调。通过 723 核心与 82 运行时合成检查；两次短预检和哥伦比娅正式采样均无异常，所有入口恢复。
哥伦比娅追踪 sessions/actor-survey-20261015-174817-489 关联到 hash 0x495B9E25 → LoadedBundle → 异步 handle 64956 → 同一 spawner/templateData → 685 对象模板克隆 → Avatar_Girl_Catalyst_Columbina_Manekin(Clone)。证据 genshin-7.1-columbina-preview-load-chain.json。角色本体的包文件事件仍未捕获；展示特效包记录不能替代本体映射。

尝试从已加载包的 ASBLD/FileReader 字段恢复路径，所有角色样本均 sourceLayoutVerified=false，未取得源路径或偏移；这条路线未验证，不能生成可用块映射。相关离线布局证据保留在 sessions/*source*review.json。

沿实际 AOT 调用栈发现 7021590→6e37aa0→6e34e30/6c1e400：64 位资源 ID 高 32 位用于 asset hash，低 32 位查 LoadedBundle 字典。首次按包目录数字查询全部失败；随后按 getter 机器码确认字典 entries 数组每项 24 字节、key 在 +8、LoadedBundle 在 +16，分批只读扫描并按完整包名验证实际键。
只读样本 sessions/actor-survey-20261015-202305-923，engineException=0、entryBytesRestored=true：

| 角色模板 | asset hash | 实际包键 |
| --- | --- | --- |
| PlayerBoy | 0x2AAED67B | 1114 |
| Linnea | 0x14DDB30E | 1179503 |
| Columbina | 0x495B9E25 | 1126875 |
| Vodyanitsa | 0xF2A07319 | 1383073 |
| Odette | 0x55F2C739 | 1300049 |

资源 ID = (uint64(assetHash) << 32) | actualPackageKey。目录数字（例如 Columbina 的 808）与实际包键不同。扫描只读运行时字典，不调用加载器；已移除无效的按目录数字试探查询。
证据 docs/evidence/genshin-7.1-actor-resource-ids.json。当前资源 ID 只验证已驻留包，仍未完成未预览角色直接加载。下一步复用游戏加载器处理未驻留包及依赖，不再猜物理块偏移。


## 2026-10-15 真实块目录与直接模板创建成功

通过游戏目录查询 710aa60 / 710ac70，取得五个已验证包键对应的块文件、偏移和长度。磁盘上对应范围均有效，五个头部均为 426c62037500000005000000（Blb、版本 5）。证据 genshin-7.1-actor-block-directory.json；不再依赖尚未验证的 ASBLD/FileReader 布局。

实验构建 build-studio-actors-v16-game-resource 通过 723 核心与 245 运行时合成检查。按实际资源 ID 查询 LoadedBundle，加载实例缓存与模板；按原生同步 Instance 顺序先 RawTemplateClone、禁用自有根对象、FinishTemplateClone，再托管包装和组件处理。此前初始化前包装导致的异常，在以下两次驻留包实验中未复现：

| 模板 | 原生模板对象 | 模型节点 | 蒙皮网格 | 清理 |
| --- | --- | --- | --- | --- |
| PlayerBoy | 335 | 153 | 6 | 完成 |
| Columbina | 685 | 294 | 11 | 完成 |

两次网格、骨骼和材质对象引用检查通过，exception=0、orphanedObject=false。证据 genshin-7.1-direct-template-resident-success.json，保留创建、健康检查、清理与会话结束状态。

限制：当前游戏进程已预览过这些角色，因此只证明可以不依赖现存角色 GameObject、从驻留包模板创建。未驻留包分支调用游戏 EnsurePackage（7015f30），尚未实机执行；依赖加载和源资源生命周期仍需验证。引用检查不能证明 shader 正确，洋红色渲染问题仍未解决。GUI 默认继续使用 v9-safe，模板功能只在显式 ActorTemplates 实验构建开放。下一次冷加载验证应正常重启游戏、直接进入场景且不打开角色预览，先执行 load-template，再决定是否创建。


## 2026-10-15 重启后的首次包加载验证

用户正常重启后，PID 11140。v16 对哥伦比娅执行 load-template 成功，再创建模板副本成功，294 节点 / 11 蒙皮引用检查通过并清理。不过 v16 未记录包是否已驻留，因此不能据此证明 EnsurePackage 分支执行。

v17-package-diagnostics 增加 resourceId、packageWasResident、packageLoadAttempted、packageLoadSucceeded 状态。奥黛塔实测 packageWasResident=false、packageLoadAttempted=true、packageLoadSucceeded=true，证明实际调用游戏首次包加载接口。随后模板克隆 513 原生对象，模型 241 节点 / 10 蒙皮；健康检查 actor-models-incomplete：missingMeshes=10、invalidMaterials=9。创建与清理均 exception=0、orphanedObject=false，会话 closed=true。

结论：主模板首次加载已打通，但完整角色冷加载尚未完成，网格与材质依赖仍缺失。不能把 actor-created 或模板名称验证等同于可见、正确渲染的角色。后续应定位游戏原生依赖编排，而非反复实例化同一不完整模板。源包归游戏所有，不主动卸载。证据 genshin-7.1-cold-template-load.json。


## 2026-10-15 依赖展开与完整引用冷加载

离线沿 EnsurePackage 的真实调用点定位同步编排 700cd80：6f36810(outList,resourceId,catalog,true) 展开依赖，反向遍历逐包调用 7015f30。6f36810 使用池化 List<int>，7011930 调用 710e250 递归展开并去重；6f368e0 归还列表。注意 700cd80 还执行引用计数及条件卸载，不直接以未知选项调用整个编排器。

v18-dependencies 只复用经版本和入口字节核验的依赖展开/归还接口，复制最多 1024 个包键，立即归还池对象，反向 Ensure 缺失包，最后加载主包和模板。不保留池对象跨回调，不主动卸载游戏源包。依赖展开自身可能递归与分配，1024 是结果接受上限，不是引擎内部递归预算；资源租约和跨帧加载仍待实现。

PID 11140 沃雅妮莎首次包原未驻留，dependencyCount=155，补载 134 个依赖包，主包加载成功。创建 605 原生对象 / 278 节点 / 16 蒙皮，健康检查 missingMeshes=0、invalidMaterials=0，清理正常、会话关闭，无异常或残留。这是首个明确进入首次加载分支且网格材质引用完整的角色样本。尚未视觉验证 shader 与最终渲染，不能认定洋红色已解决。

反例：本进程奥黛塔主模板此前已在缺少依赖时加载；补载 77 个依赖包后再次克隆仍 missingMeshes=10、invalidMaterials=9。因此必须在第一次模板解析前加载依赖，后补依赖不能自动修复本次缓存。不通过卸载游戏共享包强行修复；奥黛塔后续需新进程验证。证据 genshin-7.1-dependency-template-load.json。

构建 build-studio-actors-v18-dependencies：723 核心、245 合成运行时检查通过。GUI 默认不变，实验加载目前只支持已验证资源 ID 的五个角色；任意角色目录和资源生命周期管理未完成。


## 2026-10-15 会话资源引用生命周期

离线核对 6e36b60(manager,key,delta)：查 LoadedBundle，引用为零时从待回收集合移除，再将 +0x20 引用数增加 delta。6e372b0(manager,key)：减少一次引用，零时加入回收队列/原生回收流程。两入口加入 SHA/字节核验，避免直接写引用字段。

v19-resource-leases 以会话为单位持有主包及依赖包，最多 3072 个唯一包键；记录键与 native AssetBundle 身份，不长期保存托管对象指针。每次 retain 前检查引用有效，调用后回读 +1。正常清空演员仍持有资源，方便同会话重新创建；退出时先确认全部演员清理且无 orphan，再反向 release 各一次。释放前重新查管理器与包，核对原生身份和正引用；异常时保留未归还条目及 dirty 状态，不假报完成。退出清理等待包含资源引用，因此不会因模型已清空而提前移除 hook。

沃雅妮莎两次连续独立会话：各持有 155 个引用，模型健康通过，退出均 packages-released、剩余 0、归还 155、exception=0、orphanedObject=false、closed=true；首轮 entryBytesRestored=true。证据 genshin-7.1-package-leases.json。构建通过 723 核心与 245 合成运行时检查。尚未验证跨场景长时持有和多角色共享引用；首次 134 包同步加载的帧阻塞也尚需分帧处理。

渲染 ABI 离线继续核对：143d9b0 对输入托管对象解包，调用 163dcc0 返回原生对象，再托管包装；143cee0 调 1bcaa40 返回 bool。元数据名称仍不足以证明语义，不以错误签名主动调用。下一步需验证返回 Shader 原生类型、错误 shader 与预览组件初始化路径，不能据资源引用健康宣称洋红色已修复。


## 2026-10-15 沃雅妮莎渲染视觉确认

v20-shader-diagnostics 使用经机器码核对的 Material shader 对象 getter（143d9b0→163dcc0），调用前确认 Material 类型，返回后确认 Shader 类型，再读取名称。未调用元数据名称可疑的 bool 接口。诊断 26 个材质槽，invalidShaderReferences=0、errorShaderNames=0，名称包括 Character_Base_Uber、Character_Uber_Stockings_New、Character_Uber_Mermaid 等。名称有效不等于 shader 可渲染，故进行了必要的视觉确认。

用户在 sessions/20261015-214820-36e062f1 展示期间明确反馈“颜色和贴图正常”。沃雅妮莎完整依赖路径的洋红色问题已获视觉解决确认；未修改或保留不明预览脚本，没有盲目替换 shader。先前缺依赖与补齐后的差异支持依赖缺失解释，但不能将此结论推广到所有角色或所有渲染故障。

展示确认后立即触发检查与清理，最终 packages-released、155 引用归还、剩余 0、closed=true。证据 genshin-7.1-vodyanitsa-render-confirmed.json。奥黛塔仍需新进程首次按依赖顺序加载后验证；当前进程之前的不完整模板不能后补恢复。剩余动态工作：分帧包加载以减少首次卡顿，多角色共享资源租约与跨场景处理。


## 2026-10-15 扩展角色目录与 v21 GUI

用户重启后确认哥伦比娅、奥黛塔正常，支持此前不完整缓存解释。按用户要求接入纳西妲、桑多涅、布伦妮。只读 v32 记录实际 LoadedBundle 字典完整 20069 包身份，无截断；模板采样扩展到 256 项，支持 Loli/Male 等体型名称。新身份：Nahida hash6976fdb2/package84763；MarionetteNew hash0171df52/package1282629；Prune hashf0bc3a96/package1200296。额外记录 Yoimiya、Kachina、Nahida_Show01。此前五名保留，总计 11 项验证资源身份，19 项模板目录。

生成器按完整包名连接模板 hash 与实际包键，验证干净只读报告；增量合并同版本已有身份，冲突拒绝，不覆盖此前已采集角色。生成 JSON 角色选择数据，GUI 中文标签独立维护于 data/actor-labels.json，未命名角色显示内部选择名。当前依然需要重建运行时才能加入新的资源身份，修改 GUI 标签不会使未验证资源可加载。

模板选择优先精确 _<selector>_Manekin 后缀，解决 Nahida 与 Nahida_Show01 的子串歧义；无精确匹配时保留原多匹配拒绝行为。模型名仍与目录精确核验。v21 实机三名角色网格/材质引用完整：纳西妲 163 节点/6 蒙皮，桑多涅及布伦妮各8蒙皮。各清理正常，135/97/62 引用归还、剩余0。证据 genshin-7.1-expanded-role-tests.json。本轮资源已被预览，不能声称三名冷加载或最终画面已确认。

build-studio-actors-v21-expanded-catalog 通过723核心、245运行时合成检查，27 Python测试通过。test-direct-actors.cmd 指向 projects/actors-v21-direct.gimmd.json，新 GUI 默认该构建。额外角色未经本轮创建验证，保留实验性质。


## 2026-10-15 本地演员 MMD FK 首版

用户确认新增角色正常，唯 Nahida_Show01 展示模型报错；GUI 暂时过滤该项，不把展示道具模型当普通演员。新增独立 actor-motion 通道，一次选择一名已创建演员播放 VMD，不切换或写主角骨骼。worker 有界解析单独 actor-motion.vmd，原子占用状态和命令锁发布后，Unity 回调按槽位根对象身份、节点路径/名称/原生身份重新解析骨骼；没有跨帧保存 Transform 指针。

按现有52项映射连接实际存在的VMD轨道和唯一演员骨骼，至少10项有效映射才播放。记录实际初始局部旋转作为rest，使用已有父坐标变换矩阵计算FK，含0.5秒入场融合与下半身/上半身并行轨道补偿。轴矩阵仍来自现有标定，未验证任意角色/任意动作视觉质量。首版不包含根位移、腿脚IK、音频或相机同步；只支持一名演员动作通道，其他演员静态。

支持播放、暂停、继续、结束定格、手动恢复；清空/退出优先恢复后删除演员并归还资源。恢复失败保留会话，不继续删除。播放时读取所有目标完成后再写入，写入失败触发恢复。状态通过主线程State快照发布，worker不读取主线程可变播放字段。

两秒自制52轨道校准：纳西妲与桑多涅均通过写入、暂停保持、继续、结束定格、恢复、模型健康检查和清理；桑多涅另通过播放中清空。引用均归还至0。证据 genshin-7.1-actor-mmd-fk.json。测试发现Windows命令读句柄占用导致原子替换PermissionError，改为VMD解析前关闭命令文件，Python原子替换增加最多110毫秒有界重试；增加对应竞态与能力/槽位校验测试。

v22构建通过723核心、245运行时合成检查，29Python测试通过。GUI演员页增加槽位1–3和播放/暂停/继续/恢复按钮，默认工程使用两秒校准动作；test-direct-actors.cmd切换至v22。实际舞蹈效果待用户试用，后续需动态骨架轴标定、根位移、腿IK与多人独立时间线。


## 2026-10-15 演员腰部辅助骨与根位移 v23

用户反馈宵宫、奥黛塔转体时腰部不跟随，要求根位移。演员通道此前只驱动标准52映射，没有旅行者的 +PelvisTwist 辅助骨处理。现以实际骨架查找与Bip001 Pelvis同父节点的 +PelvisTwist 前缀辅助骨（上限16），记录各自初始旋转，以当前pelvis相对其初始旋转的delta驱动各helper，不绑定旅行者固定AvatarReference索引。写前验证路径身份，helper参与失败恢复、手动恢复及清空/退出清理。

增加根轨道解析：全ての親、センター、グルーブ；沿用已验证root_controls合成及root_translation的EIEM源坐标基转换。无Center时可使用烘焙腰轨道位移，避免旋转重复应用；主控/Groove非单位旋转仍拒绝。通过实际Body局部旋转和缩放换算，仅写自有模型Bip001局部位置，不移动游戏角色实体。首帧位移锚定原站位，保留0.5秒入场融合；Center旋转复合到pelvis，上半身补偿只消除下半身独立旋转，不消除Center的整身转体。所有接口按现有SHA与入口字节校验。暂停/结束定格保留位置，恢复时回写原局部位置并验证。

新增可复现scripts/generate-actor-calibration.py，生成52FK轨道+Center横移/起伏/转体的2秒动作。宵宫与奥黛塔实测各helperCount=1、helperWrites=81/82、rootWrites=81/82、maximumRootOffset=0.279924；暂停、继续、结束定格、恢复和播放中清空均通过，资源引用剩余0。证据genshin-7.1-actor-root-pelvis.json。腰部实际舞蹈视觉效果仍待用户确认，脚部IK尚未接入演员通道。

v23通过723核心、245合成运行时、29Python检查。GUI显示腰部辅助骨数量和根位移来源状态，默认工程使用新的校准动作；test-direct-actors.cmd切换到v23。
