# GIMMD prototype

原神客户端内 MMD 创作工具原型。当前提供 VMD 检查/采样、统一时间线、Windows 加载诊断、角色骨架观察、绑定预览及显式启用的单骨恢复与限时单轨驱动实验；稳定的可见动作播放、表情及相机控制尚未完成。

2026-10-12 已在指定的国服 7.1.0 客户端完成两秒 VMD 单轨采样、43 次写入和停止恢复。此前已验证左上臂 1° 同回调写入/恢复、读取 465 节点完整层级并生成 52 项绑定候选。最新进展见 [单轨驱动实测](docs/SINGLE_TRACK_PLAYBACK_RESULTS.md)，前阶段见 [单骨恢复](docs/SINGLE_BONE_ROUNDTRIP_RESULTS.md)、[骨架与旋转](docs/HIERARCHY_OBSERVER_RESULTS.md)、[相机观察](docs/CAMERA_OBSERVER_RESULTS.md) 和 [加载诊断](docs/FIRST_PROTOTYPE_RESULTS.md)。

目标：Windows x64；用户提供的客户端为国服 7.1，`E:\Game\Genshin Impact Game\YuanShen.exe`。

## 构建与测试

需要 Visual Studio C++ x64 工具与 Windows SDK，无外部包下载：

```powershell
.\scripts\build.ps1 -Test
.\build\gimmd-vmd.exe .\build\synthetic.vmd 0.5
.\scripts\test-loader.ps1
.\scripts\inspect-pe-patterns.ps1 -SelfTest
```

最新进展：[全身 FK 与播放卡顿修复](docs/FULL_BODY_FK_RESULTS.md)。已实机完成 52 个目标骨骼、完整旋转幅度与音频联动，使用 EIEM 默认源预设近似和已验证目标参考骨架。共享骨架路径后，每批姿态平均耗时从 43.49 ms 降至 2.59 ms，用户确认“明显改善，基本流畅”；完整播放后全部恢复。Root/Center 位移、脚部 IK、表情和相机尚未接入。

[目标参考骨架恢复](docs/TARGET_REST_POSE_RESULTS.md) 取得 139 条参考骨骼记录，与 Body 的 91 组绑定矩阵交叉验证，52 项目标映射均具备局部参考姿态，包含负缩放处理。

[时间轴控制与音频联动](docs/PLAYER_SESSION_RESULTS.md) 已实机验证暂停、跳转、继续、循环、停止和 WAV 播放。原十骨骼 5° 相对预览仍保留为默认实验策略；全身模式需显式启用 `-FullBodyFK`。支持完整源时间轴，单次会话最多 90 秒。实验选项默认关闭，控制命令和限制见记录。[前阶段十骨骼预览](docs/MULTI_TRACK_PREVIEW_RESULTS.md)。

## 接入探测

以下为默认的模块/导出探测构建。可选的运行时观察器使用：

```powershell
.\scripts\build.ps1 -RuntimeProbe -Test -OutputDirectory build-runtime-next
```

运行时观察器仅支持 profile 固定的 EXE 指纹，临时 Hook FOV 回调，在游戏线程采样相机、角色层级与旋转后停用；不会写入姿态或相机。加载命令同下，路径改为对应输出目录。详见上述实测记录。CMake 对应选项为 `-DGIMMD_RUNTIME_PROBE=ON`，默认 OFF。

单骨写入/恢复实验需要额外显式传入 `-PoseRoundTrip`，或 CMake 的 `-DGIMMD_POSE_ROUNDTRIP=ON`；默认关闭。使用新输出目录构建后，才可按下方方式加载对应 DLL。实验最多消费一次，细节和实测限制见单骨恢复记录。

`gimmd-probe.dll` 的 DllMain 只记录模块句柄，不启动线程、不调用游戏。加载工具显式调用 `GimmdProbe`，报告写入 DLL 同目录的 `gimmd-<PID>.probe.json`。只检查已加载模块与导出是否存在，不调用 IL2CPP、不扫描地址、不修改游戏代码。

```powershell
# 已运行进程：替换 1234 为实际 PID
.\build\gimmd-inject-probe.exe --pid 1234 .\build\gimmd-probe.dll

# 启动指定客户端并在 10 秒后尝试普通 LoadLibrary 加载
.\build\gimmd-inject-probe.exe --launch 'E:\Game\Genshin Impact Game\YuanShen.exe' .\build\gimmd-probe.dll
```

加载器仅接受原神或自建 probe-host，使用普通 Windows API；没有提权、保护绕过或自动重试。失败保留游戏进程并报告错误。DLL 本轮保持加载至进程退出；超时的远程线程不强制结束，其参数内存保留至进程退出，避免仍运行的线程访问已释放参数。

此客户端启动需要管理员权限，直接启动返回 Win32 740。已通过 Windows UAC 手动授权运行加载器完成实测；加载器本身不请求提权。重复诊断同样需要与目标匹配的权限。正在使用的 DLL 不能原地覆盖：退出游戏后重建，或使用 `scripts/build.ps1 -OutputDirectory build-next` 输出下一代构建。

只读对照 FufuLauncher 特征与本机 EXE 文件：

```powershell
.\scripts\inspect-pe-patterns.ps1 -Executable 'E:\Game\Genshin Impact Game\YuanShen.exe' -Output .\build\patterns.json
```

报告中的 RVA 是文件候选，不能直接用作可调用运行时接口；此脚本不读取或写入进程内存。

FufuLauncher 的 Plugins/File 配置只保证加载 DLL；本探测 DLL 需要显式调用导出，当前不宣称即插即用兼容其插件系统。源码调研和实测记录见 docs。

## 来源

VMD 解析与采样来自 Better-Endfield 的固定提交，保留了其 EIEM 来源说明；详见 `third_party/better_endfield/UPSTREAM.md`。工程采用 AGPL-3.0-only。运行时观察器引用 FufuLauncher 固定提交的特征掩码与已核对的相机调用方式，使用官方 MinHook v1.3.4；来源分别见 `src/generated/gi_profile.h` 与 `third_party/minhook/UPSTREAM.md`。
