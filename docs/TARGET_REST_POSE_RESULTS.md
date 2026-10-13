# 目标参考骨架恢复（2026-10-13）

后续已将这些数据接入全身 FK，并修复播放卡顿，见 [最新实测](FULL_BODY_FK_RESULTS.md)。下文记录只读参考恢复阶段。

已取得原神当前男主模型的可信参考骨架，52 个预定映射都有局部位置、旋转和带符号缩放。数据来自资源参考姿态，不把角色待机动作或 VMD 第 0 帧当作中性姿态。本阶段只读，没有扩大实机动作幅度，也没有接入全身重定向写入。

## 关键发现

EIEM 的 `GhostRig_ReadSkeletonMetadata` 依赖 `Avatar.humanDescription.skeleton`。当前国服 7.1.0 中，角色 `Animator.isHuman=true`，但 Avatar 只保留五项定制方法，没有 `get_humanDescription`。因此不能直接搬用 EIEM 的读取路径。

已核对命名方法和本机指令链后，采用两份独立参考交叉验证：

1. `SkinnedMeshRenderer.bones` 与 `sharedMesh.bindposes` 按索引对应，取得 Body 的 91 个逆绑定矩阵，求逆恢复网格空间参考姿态。Unity 对应语义见 [Mesh.bindposes 文档](https://docs.unity3d.com/ScriptReference/Mesh-bindposes.html)。仅 Body 可覆盖 49/52 个目标，缺少骨盆和左右上臂；面部与 EffectMesh 没有补齐它们。
2. 原神定制 Avatar 的 `BoneNameToIndex`、`GetBoneDefaultPosCharacterSpace` 指令链引用完整参考骨架：父索引及 48 字节的局部位置/四元数/缩放记录。通过固定 EXE 指纹和逐入口字节检查，只读复制这份资源数据，并用命名索引关联当前 Transform。
3. 对 Avatar 局部参考做完整 FK，使用共同骨骼对齐到 Body 网格坐标；检查所有 91 个共同骨骼的矩阵一致性，确认后才接纳蒙皮未使用的骨骼。

| 项目 | 结果 |
|---|---:|
| Avatar 参考骨骼记录 | 139 |
| 与当前角色节点关联 | 121 |
| Body 绑定矩阵 | 91 |
| 共同骨骼交叉验证 | 91 |
| FK 与绑定矩阵最大分量误差 | 0.00015985 |
| 52 项目标的参考姿态 / 局部参考姿态 | 52 / 52 |
| 参考数据中的负缩放记录 | 6 |
| 游戏姿态 setter 调用 | 0 |
| 引擎异常 / 未恢复 Hook | 0 / 无 |

最大分量误差包括旋转、缩放和位置，不能直接解释为距离或角度。当前接纳阈值为 0.0002。引用坐标系是 Body 网格空间，尚未完成到角色移动根/世界坐标的校准。

负缩放是关键差异：不能把所有骨架变换简化成四元数乘法，或把负缩放直接改成正值。导出保留原始局部 TRS 和完整矩阵；有反射的网格空间变换不强行导出为正缩放四元数。

## 代码与证据

- `src/windows/bind_pose_observer.h`：显式启用的只读探测。验证组件所有者、命名入口与固定指令、数组数量与类型、父索引范围、节点所属角色及重复读取一致性。数据只保存数值、名字和节点索引，不在回调间保存引擎对象句柄。
- `scripts/build-rest-pose.py`：逆绑定矩阵恢复、多网格参考对齐、带符号 TRS 的 FK、Avatar/蒙皮交叉验证和 52 项映射导出。
- [完整参考读取报告](evidence/genshin-7.1-avatar-reference-signed.json)、[导出的目标参考骨架](evidence/playerboy-target-rest-pose.json)、[验证清单](evidence/target-rest-validation.json)。
- [Avatar 命名方法](evidence/genshin-7.1-avatar-description-methods.json)、[参考存储指令](evidence/avatar-default-pose-storage.json)、[蒙皮数组指令](evidence/mesh-bindposes-array-disassembly.json)。

本轮执行 7 项新增数学与接纳检查，覆盖矩阵求逆、带缩放旋转、父子变换、反射、负缩放链以及参考不一致时拒绝；现有 53 项运行时观察检查通过。主要验证依靠真实数据的两份参考交叉比较，没有重跑无关构建矩阵。一次层级采样达到原有时间预算而拒绝继续，后续完整采样成功。

## 重现

```powershell
.\scripts\build.ps1 -RuntimeProbe -AnimatorProbe -BindPoseProbe -OutputDirectory build-rest-next
# 使用与游戏匹配权限的终端；替换 PID
.\build-rest-next\gimmd-inject-probe.exe --pid 6020 .\build-rest-next\gimmd-probe.dll
python .\scripts\build-rest-pose.py .\build-rest-next\gimmd-6020.probe.json .\build-rest-next\target-rest.json
```

`-BindPoseProbe` 默认关闭，要求 Animator 观察或租约模式；CMake 对应 `GIMMD_BIND_POSE_PROBE=ON`。当前已验证的是 `Avatar_Boy_Sword_PlayerBoy`，不是所有角色通用的已验收配置。读取不依赖世界暂停。

## 对全身重定向的影响

目标侧参考数据已齐。后续应以目标参考局部 TRS 和父级参考基为基础，处理坐标转换与带符号缩放；不得用当前待机旋转替代参考姿态。新增读取的 EIEM `direct_vmd_pose.h` 明确采用绕 Y 轴 180° 的源坐标基，并为躯干、上肢和手腕提供源方向参考；这些是 EIEM 的源预设，尚未证明能原样用于本动作和原神模型。

仍需完成源骨架/局部轴与手掌方向校准、全身 FK 输出、Root/Center 位移、腿部 IK 和捩骨分配。现有实机播放器继续维持十骨骼 5° 的相对预览，不把本次只读成功等同于完整舞蹈播放成功。
