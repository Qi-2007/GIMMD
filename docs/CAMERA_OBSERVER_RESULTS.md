# 第二阶段：正式客户端相机观察实测

日期：2026-10-12，Asia/Shanghai。接续 [首轮记录](FIRST_PROTOTYPE_RESULTS.md)。用户已手动进入可见角色的游戏场景。

## 实测结果

目标为国服 7.1.0，PID 9620，EXE SHA-256：
`7F89938DA606C1281659607D464702CDDB9A09A7D4320A196630F60A811EC38E`。

[原始实机报告](evidence/genshin-7.1-camera-observer.json) 已从运行目录保存。结果为 `camera-read-observed`：

| 项目 | 观察结果 |
|---|---|
| 构建指纹及运行时掩码 | 指纹一致，13 个文件候选的运行时掩码均匹配 |
| FOV 回调 | 102 次，均发生于 Unity 窗口线程 26892 |
| 相机采样 | 4 次，每次 3 个相机，位置均可读 |
| Camera.main | 位置 `[0, 0, -2]` |
| 另外两个相机 | 位置均约 `[5.80915, 195.32233, 3.32284]` |
| 场景根节点 | 每次均找到 `/EntityRoot` 与 `/EntityRoot/AvatarRoot` |
| 引擎调用异常 | 0 |
| Hook 停用 | 成功，入口原有 32 字节恢复，报告时活动回调为 0 |
| 姿态/相机 setter | 未调用 |

位置结果支持上游“Camera.main 可能指向 UI 相机”的判断，但不能单凭位置确认实际渲染相机。两个非 main 相机位置几乎相同；尚未确认用途、激活状态、投影或渲染顺序。没有把它们自动绑定为 MMD 镜头。

`AvatarRoot` 存在只证明根节点查询路径可用，尚未找到当前角色、枚举骨架或绑定骨骼。当前仍不能在游戏中播放 MMD。

## 实现与边界

`src/windows/runtime_probe.cpp` 是显式启用的观察器，普通构建继续使用原来的模块/导出探测器。

1. DllMain 仅记录模块句柄。加载器调用导出 `GimmdProbe` 才开始工作。
2. 先计算目标 EXE 的完整 SHA-256，再核对固定 profile 中各候选在进程内的掩码、可执行区域和跳转目标。构建不符时不安装 Hook。
3. 仅临时 Hook 已核对的 FOV 入口；原始参数、原始调用结果保持透传。游戏函数只在观察到的 Unity 窗口线程回调中调用。
4. 每 250ms 至多采样一次，共最多 4 次；等待回调的窗口最多约 5 秒，之后关闭采样并停用自己的 Hook。
5. 采用上游 x64 数组布局假设，读取元素数上限为 16，位置必须有限。SEH 捕获引擎异常后停止采样。这些检查降低误调用影响，不能证明所有场景的对象生命周期安全。
6. 停用后保留 DLL 与失活 trampoline 到游戏退出，避免延迟回调进入已释放代码。不主动卸载 DLL，不强制关闭游戏。

MinHook 来自官方 v1.3.4，保留 BSD 许可。特征来自 FufuLauncher.UnlockerIsland 固定提交 `2433a01d4fb4f96d340e82d035750e55b6a2c534`，保留 AGPL 来源。未使用 `GameUpdate` 作为每帧驱动；上游实际在 FOV 回调调度相机更新。

## 构建与验证

```powershell
.\scripts\build.ps1 -RuntimeProbe -Test -OutputDirectory build-runtime-next
```

已完成：657 项核心检查、16 项独立观察器检查、未知构建拒绝检查，以及上述正式客户端实测。独立观察器检查覆盖真实 MinHook 透传与停用、数组上限、异常后停止和入口字节恢复；自建宿主不能替代游戏兼容性验证。

也可通过 CMake 选择 `-DGIMMD_RUNTIME_PROBE=ON`；默认值为 OFF。已用 Visual Studio 18 2026 的 Release 配置构建并通过 CTest 的 core、runtime-observer 两项测试。CMake 的 `/W4` 输出包含未修改的 MinHook 上游 C 源码警告。正在使用的 DLL 不可原地覆盖，后续构建应输出到新目录。

```powershell
cmake -S . -B build-cmake-runtime -G "Visual Studio 18 2026" -A x64 -DGIMMD_RUNTIME_PROBE=ON
cmake --build build-cmake-runtime --config Release --parallel 1
ctest --test-dir build-cmake-runtime -C Release --output-on-failure
```

## 下一阶段验收条件

1. 对 `GetName` 和旋转读取各两个候选进行调用点/包装器消歧，不能任取第一个匹配。
2. 确认 `Transform.GetChild` / `childCount` 或等效骨骼枚举路径及 ABI，再在同一游戏线程有界读取角色层级。对 EXE 的初步 ASCII/UTF-16 搜索未发现常见明文接口名，不能直接采用按名称查找的方案；这不证明接口不存在。
3. 区分实际画面相机与其他相机，确认切场景及角色切换后的对象失效规则。
4. 先记录原姿态，再做单骨、单帧的写入与恢复实验；恢复通过后连接 VMD 时间线。相机写入也应单独验证恢复，不能用本轮入口字节恢复结果代替姿态恢复。
