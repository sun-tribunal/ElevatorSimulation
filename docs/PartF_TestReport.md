# F 部分（数据统计、系统测试与算法评价）测试报告

- 生成日期：2026-09-02（含按 F 需求文档补齐后的复验）
- 任务范围：仅修改 `Statistics/*` 与 `Tests/*`（含测试源在 vcxproj/filters 的必要登记）；不修改 Core/UI 等其他代码
- 依据：《任务开发说明》F 部分需求 + README F 模块方向；统一使用公共 `Direction`/`PassengerState`/`ElevatorState`/`SimulationConfig`，未重复定义任何公共类型

---

## 1. 需求符合性对照

| 需求项 | 实现 | 满足 |
| --- | --- | --- |
| 累计产生乘客人数 | `totalPassengerCount`（自 Reset 起累计） | ✅ |
| 当前等待人数 | `waitingCount` | ✅ |
| 当前乘梯人数 | `ridingCount` | ✅ |
| 已到达人数 | `arrivedCount` | ✅ |
| 平均等待时间（含上梯 T） | `averageWaitingTime` | ✅ |
| 最大等待时间 | `maxWaitingTime` | ✅ |
| 平均乘梯时间（含下梯 T） | `averageRideTime` | ✅ |
| 每梯总运输人数 | `ElevatorStatisticsSnapshot::transportedCount` | ✅ |
| 每梯总运行楼层数 | `traveledFloors` | ✅ |
| 每梯空载运行楼层数 | `emptyTravelFloors` | ✅ |
| 每梯满载次数 | 新增 `Statistics::GetFullLoadCount`（连续满载时段计数）+ `FormatSummary` 输出 | ✅ |
| 每梯工作时间 | 新增 `Statistics::GetWorkingTime`（非空闲累计）+ `FormatSummary` 输出 | ✅ |
| 每梯空闲时间 | `idleTime` | ✅ |
| 异常情况检查 | 见第 3 节（超载、错误掉头、重复分配、目标层错误、等待乘客丢失） | ✅ |
| 客流测试：低/正常/高 | SystemTests 三个客流场景（λ=0.05 / 0.6 / 8） | ✅ |
| 算法对比（有条件时） | `DispatchComparison` 基线(0fade61 贪心固定归属) vs 当前(联合分配+动态改派) 8 场景表格 | ✅ |
| 典型情况测试（9 类） | SystemTests 逐一覆盖，见第 3 节 | ✅ |
| 提交 Statistics 代码 | `Statistics.h/.cpp`（含新增展示/满载/工作统计） | ✅ |
| 提交系统测试记录 | 本报告 | ✅ |

---

## 2. 交付内容

| 交付物 | 说明 |
| --- | --- |
| `Statistics::FormatSummary()` | 只读文本汇总（总量/均值/分梯：送达、移动、空驶、**满载次数**、满载时长、空闲、**工作时间**），供测试与未来 UI 展示 |
| `Statistics::GetFullLoadCount(id)` / `GetWorkingTime(id)` | 单梯满载次数（连续满载时段计数，避免逐帧重复）与工作时间（非空闲累计秒）；越界抛 `out_of_range` |
| `Tests/StatisticsTests.cpp` | 单元测试套件，17 场景 / 43 断言 |
| `Tests/SystemTests.cpp` | 系统测试套件，17 场景 / 50 断言（典型情况 + 客流 + 异常 + 统计结果） |
| `Tests/DispatchComparison.cpp` | 对照场景扩至 8 个（新增 evening_peak / interfloor / saturation） |
| `Tests/RunCoreTests.cmd` 等 | Statistics、System 套件登记；vcxproj/filters 按既有模式登记 |

### FormatSummary 实际输出样例（临时程序实测）

以下为以典型事件序列驱动 `Statistics` 后 `FormatSummary()` 的真实输出（4 人产生、3 人完成上梯含上梯 T、2 人到达含下梯 T；6 梯移动/耗时片段）：

```text
总乘客=4 等待中=1 乘梯中=1 已到达=2 平均等待=6.50s 最大等待=9.00s 平均乘梯=15.25s
E1: 送达=1 移动=3 空驶=1 满载=2次 满载时长=55.00s 空闲=50.00s 工作=95.00s
E2: 送达=0 移动=1 空驶=0 满载=0次 满载时长=0.00s 空闲=90.00s 工作=0.00s
E3: 送达=1 移动=1 空驶=0 满载=0次 满载时长=0.00s 空闲=0.00s 工作=70.00s
E4: 送达=0 移动=0 空驶=0 满载=0次 满载时长=0.00s 空闲=145.00s 工作=0.00s
E5: 送达=0 移动=0 空驶=0 满载=0次 满载时长=0.00s 空闲=145.00s 工作=0.00s
E6: 送达=0 移动=0 空驶=0 满载=0次 满载时长=0.00s 空闲=145.00s 工作=0.00s
```

数值核对：平均等待=(4.0+6.5+9.0)/3=6.50s；最大等待=9.00s；平均乘梯=(12.0+18.5)/2=15.25s；E1 两段连续满载（30s+25s）→ 满载=2次、满载时长=55s；E1 工作=40+30+25=95s（非空闲累计）、空闲=50s。

---

## 3. 系统测试覆盖（SystemTests）

**典型情况（9 类）**：无乘客、单乘客、多乘客、高峰客流、电梯满载（容量 1）、多梯空闲、反方向请求、顶层/底层请求、长时间运行（3600s）。

**异常检查**：
| 异常 | 检测方式 | 断言 |
| --- | --- | --- |
| 超载 | `BeginBoarding` 满载时拒绝 / `CanBoard=false` | ✅ 满载后第三人不被允许 |
| 错误掉头 | 前方任务未清时方向锁定 | ✅ 方向保持 Up |
| 重复分配请求 | Simulation 外呼按 (楼层,方向) 唯一 + Elevator 任务去重 | ✅ 3 人同方向 = 1 个外呼；重复 AddHallCall 去重 |
| 目标层错误 | 反方向目标/越界目标拒绝 | ✅ `BeginBoarding` 与 `AddInternalTarget` 拒绝 |
| 等待乘客丢失 | 传送中队列保留、完成后无遗留 ID/外呼 | ✅ 上梯 T 内队头不丢；结束后快照为空 |

**客流**：低（λ=0.05，3600s）、正常（λ=0.6，600s）、高（λ=8，600s）均生成并送达，均值有限，`ValidateState` 通过。

---

## 4. 测试执行结果（双架构）

环境：MSVC v143、`/std:c++17 /W4 /WX /utf-8 /MDd`，独立编译 Core/Statistics、无 MFC。

| 套件 | x64 | x86 |
| --- | --- | --- |
| **Statistics（新增）** | 17 场景 / 43 断言 / 0 失败 | 17 / 43 / 0 失败 |
| **System（新增）** | 17 场景 / 50 断言 / 0 失败 | 17 / 50 / 0 失败 |
| **Reliability（新增）** | 20 场景 / 80 断言 / 0 失败 | 20 / 80 / 0 失败 |
| Dispatcher | 83 / 444 / 0 失败 | 83 / 444 / 0 失败 |
| Elevator | 26 / 87 / 0 失败 | 26 / 87 / 0 失败 |
| Simulation | 41 / 2149 / 0 失败 | 41 / 2149 / 0 失败 |
| Floor | 10 / 35 / 0 失败 | 10 / 35 / 0 失败 |
| 冒烟基线 | 406 项通过 | 406 项通过 |

合计每架构 **214 场景 / 2888 断言 + 406 冒烟项**，全部通过。四配置重建 0 警告 0 错误。

---

## 5. 算法对比（基线 0fade61 贪心固定归属 vs 当前联合分配+动态改派，x64，3 次平均）

| 场景 | 基线均等候(s) | 当前均等候(s) | 基线耗时(ms) | 当前耗时(ms) | 送达 旧/新 |
| --- | ---: | ---: | ---: | ---: | ---: |
| two_calls | 13.0000 | 12.0000 | 0.03 | 0.11 | 2/2 |
| new_detour | 41.0000 | 12.0000 | 0.02 | 0.05 | 2/2 |
| finite90 | 13.0889 | 12.6333 | 1.25 | 15.09 | 90/90 |
| batch2000 | 306.5200 | 300.5587 | 53.74 | 490.83 | 2000/2000 |
| poisson321 | 60.5609 | 58.9698 | 101.31 | 4598.82 | 3381/3422 |
| evening_peak | 44.7113 | 44.1593 | 24.76 | 60.96 | 300/300 |
| interfloor | 20.6353 | 20.0477 | 3.60 | 48.35 | 300/300 |
| saturation | 79.0572 | 76.7698 | 23.10 | 251.00 | 600/600 |

**结论**：当前算法在 8 个场景中均等候全部不劣于基线（含历史反例 new_detour 41→12），送达量持平或更高；代价是计算耗时增加（联合分配搜索），非实时性能保证。另 ElevatorTests 提供"方向优先=10s vs 最近梯=30s"单场景对照。

---

## 6. 公约合规

| 检查项 | 判定 |
| --- | --- |
| 仅修改 Statistics/* 与 Tests/*（含必要工程登记） | ✅ git status 实证 |
| 既有接口零破坏 | ✅ 新增均为纯增量；Statistics 既有方法/快照/脚本 406 项未变 |
| 统一公共类型、不重复定义 | ✅ 未新增任何 Direction/PassengerState/ElevatorState/配置副本 |
| 独立运行 | ✅ `RunCoreTests.cmd Statistics/System <arch>` 可单独运行 |
| 命名规范（PascalCase/camelCase/中文注释/STL/无裸 new/delete） | ✅ |
| 四配置重建 | ✅ 0 警告 0 错误 |
| 不虚报 | ✅ 满载次数语义（连续时段计数）与耗时边界如实记录 |

---

## 7. 结论

- F 需求全部满足：总量/均值/分梯统计（含满载次数、工作时间）、9 类典型情况、5 类异常检查、低/正常/高客流、算法对比表格、Statistics 代码与系统测试记录均已交付
- 双架构 7 套件 214 场景 / 2888 断言 + 406 冒烟项，0 失败
- 满载次数/工作时间以 Statistics 内部跟踪 + 只读接口/文本汇总提供（未改公共快照类型，避免触碰 Core/CommonTypes.h）；如需进公共快照供 UI 直接读取，需另行授权修改 Core
- 完整测试数据集见 [docs/F_TestData.md](file:///e:/360MoveData/Users/sun/Documents/GitHub/ElevatorSimulation/docs/F_TestData.md)（13 场景汇总 + 分梯明细 + 一致性核验）

---

## 8. 今日改动总览（2026-09-06）

| 提交/文件 | 内容 | 范围 |
| --- | --- | --- |
| `0725e1e`（A 部分） | `Core/Floor` 新增 `EnqueueBatch`/`Contains`（批量交通输入）；`Tests/FloorTests.cpp`（10/35）；RunCoreTests.cmd + vcxproj/filters 登记；`docs/PartA_TestReport.md` | Floor/Tests/登记/文档 |
| `8e8a819`（F 部分） | `Statistics` 新增 `FormatSummary`/`GetFullLoadCount`/`GetWorkingTime`；`StatisticsTests`（17/43）、`SystemTests`（17/50）、`ReliabilityTests`（20/80）；`DispatchComparison` 扩至 8 场景；RunCoreTests.cmd + vcxproj/filters 登记；`docs/PartF_TestReport.md`、`docs/Reliability_TestReport.md` | Statistics/Tests/登记/文档 |
| 未提交 | `docs/F_TestData.md`（13 场景测试数据集） | 文档 |

**检验结果**：双架构 7 套件 214 场景 / 2888 断言 + 406 冒烟项全通过；四配置 0 警告 0 错误；对照脚本 8 场景基线 vs 当前均等不劣于基线。

**工作区说明**：`docs/Reliability_TestReport.md` 当前工作区为空（-120 行，相对已提交版本）；`docs/Dispatcher_TestReport.md` 当前不存在。两者均为文档，不影响代码与测试；如需恢复/重建请告知。
