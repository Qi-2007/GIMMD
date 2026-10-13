# 首轮原型与实机记录

日期：2026-10-12（Asia/Shanghai）。目标：用户指定的原神国服正式客户端。

本文件保留首轮加载诊断的历史结果。后续已完成场景内相机与根节点读取，见 [第二阶段实测](CAMERA_OBSERVER_RESULTS.md)。

## 已验证结果

1. 本机 `E:\Game\Genshin Impact Game\config.ini` 确认 `game_version=7.1.0`、`channel=1`、`sub_channel=1`。EXE 的 `2017.4.30.0` 字段是引擎版本，不用它识别游戏内容版本。
2. 使用 Visual Studio 2026 的 MSVC 14.51 x64 工具构建五个目标，`/W4 /WX` 通过。
3. VMD 解析、采样和纯核心时间线共 657 项断言检查通过；其中大量检查覆盖截断文件及失败后输出不变。另验证 CLI 输出 JSON 与预期采样值一致。
4. 自建宿主中完成本地 LoadLibrary、远程 LoadLibrary、探测报告及第二次调用；测试宿主通过事件正常退出。
5. 指定游戏直接启动失败：`CreateProcessW` 返回 Win32 740（需要权限提升）。通过 Windows UAC 授权运行加载器后，成功启动并加载 `gimmd-probe.dll`，PID 为 9620；加载器返回 0。
6. 第二次通过 UAC 对同一 PID 调用探测，返回 0，生成后续快照。游戏中的角色、世界、登录操作未由工具自动执行，不能据此确认已进入可创作场景。
7. 离线 PE 特征检查器的通配符、RVA 映射、不匹配、多匹配及边界检查通过。对本机 EXE 的 10 个选定 FufuLauncher 特征进行只读扫描，得到 8 个唯一候选和 2 个歧义项。

诊断 DLL SHA-256：`DC75FE0A70B4805C28C1600EBB705135F415F59F442EE05206D8FA0BBF1A9532`。

游戏 EXE SHA-256：`7F89938DA606C1281659607D464702CDDB9A09A7D4320A196630F60A811EC38E`。后续更新应重新检查，不能只使用“7.1”标签复用结果。

## 接口可见性

[实机探测 JSON](evidence/genshin-7.1-probe.json) 记录：

- 主 EXE 已加载；在该快照中，未发现独立的 `UnityPlayer.dll`、`GameAssembly.dll`、`UserAssembly.dll`。
- 对上述四个候选模块检查的七个 `il2cpp_*` 导出均未找到。
- 探测没有调用引擎函数，没有定位托管对象，没有安装 Hook，没有修改游戏代码。

因此，参考 BE 的命名 IL2CPP 导出桥接目前没有可用证据。该结果仅说明这些候选模块和导出在此快照中的情况，不证明原神没有其他内部接口，也不证明后续进入世界时的对象布局。

## FufuLauncher 参考范围

仓库：https://github.com/FufuLauncher/FufuLauncher.UnlockerIsland

固定提交：`2433a01d4fb4f96d340e82d035750e55b6a2c534`。

- `Launcher/Launcher.cpp` 使用 `VirtualAllocEx`、`WriteProcessMemory`、`LoadLibraryW` 和 `CreateRemoteThread`；`Launcher_2` 使用挂起进程加 APC。
- 本原型选择普通 LoadLibrary 线程方式，重新实现了错误处理、x64/目标检查、超时、导出所在模块重定位与加载后验证。
- DLL 是否加载以目标完整模块路径验证，不用截断成 DWORD 的线程返回值冒充 64 位 HMODULE。
- 仅在明确调用 `GimmdProbe` 时生成报告，DllMain 保持最小行为。不会因 Fufu 的 File 配置加载 DLL 就自动开始游戏操作。
- Fufu 的相机/对象访问依赖特征定位及调用约定；其任意游戏修改和网络功能未移植、未运行。

[离线特征报告](evidence/genshin-7.1-offline-patterns.json)：

| 特征 | 文件匹配数 | 结论 |
|---|---:|---|
| FindGameObject | 1 | 唯一文件候选 |
| FindString | 1 | 唯一文件候选 |
| GameUpdate | 1 | 唯一文件候选 |
| GetName | 2 | 有歧义，不能自动选第一个 |
| FreeCamCameraGetMain | 1 | 唯一文件候选 |
| FreeCamComponentGetTransform | 1 | 唯一文件候选 |
| FreeCamTransformGetPosition | 1 | 唯一文件候选 |
| FreeCamTransformGetRotation | 2 | 有歧义，不能自动选第一个 |
| FreeCamTransformSetPosition | 1 | 唯一文件候选，未调用 |
| FreeCamTransformSetRotation | 1 | 唯一文件候选，未调用 |

匹配器只扫描文件中的可执行 PE 节，不执行源码，不修改文件，不读取进程内存。长特征的匹配起点可能只是包装器或跳板簇，必须核对上游的偏移/跳转处理、ABI 与目标函数语义；唯一匹配不等于入口已经正确。

## 代码与构建产物

- `third_party/better_endfield/vmd.h`：固定提交的 VMD 解析/采样，未修改。
- `src/core/timeline.h`：单时钟、暂停、跳转、循环与 epoch；尚未与游戏或音频连接。
- `build/gimmd-vmd.exe`：读取标准 VMD，输出轨道统计和指定时间的骨骼采样。骨骼名称以原始 CP932 十六进制输出，避免错误转码合并轨道。
- `build/gimmd-probe.dll`：仅记录运行时模块/导出可见性。
- `build/gimmd-probe-host.exe` 与 `scripts/test-loader.ps1`：自建宿主接入测试。
- `build/gimmd-inject-probe.exe`：对指定原神/宿主加载探测 DLL 并显式调用导出。
- `scripts/inspect-pe-patterns.ps1`：离线文件候选检查。

## 尚未完成及下一步

P0 已完成“DLL 加载到指定客户端”，未完成“角色和相机可控并可靠恢复”。当前不能播放游戏内 MMD。

下一轮按依赖顺序实施：

1. 核对上游 FreeCamera 包装器定位及函数签名，消除 `GetName` 和旋转读取歧义；将固定构建指纹与候选验证策略写入 GI profile。
2. 在 DLL 内只读验证候选运行时位置和指令，先不调用 setter。错误、多匹配或构建改变时拒绝使用。
3. 确认游戏线程回调位置，再在正确线程读取当前相机/对象信息。探测线程不能直接调用 Unity 对象接口。
4. 获取当前角色层级、中性姿态、对象代际，完成一次单骨姿态与相机控制的可恢复实验。
5. 以上通过后连接已有 VMD 核心，构建单人播放切片。

本轮保留游戏运行状态，未强制关闭进程。诊断 DLL 没有持续任务，但保持加载至进程退出。重新构建该 DLL 前需退出对应游戏，或将新构建输出到新目录。
