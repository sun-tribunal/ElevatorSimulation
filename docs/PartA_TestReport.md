# A 部分（Passenger/Floor）合规与测试报告

- 生成日期：2026-09-02
- 复验日期：2026-09-02（重跑 Floor 套件与冒烟测试，双架构）
- 被测版本：工作树 `A` 分支（`git diff` 仅含本轮 A 部分改动）
- 报告性质：交付物合规审计（命名规则 / 接口 / 公约）+ 测试执行结果 + README 模块状态核验

---

## 1. 报告概述

| 项 | 说明 |
| --- | --- |
| 被测模块 | `Core/Floor`（新增 `EnqueueBatch`、`Contains`）、`Core/Passenger`（未改动，随套件回归） |
| 交付物 | `Core/Floor.h/.cpp` 增量修改；`Tests/FloorTests.cpp` 新增；`Tests/RunCoreTests.cmd`、`ElevatorSimulation.vcxproj/.filters` 登记 |
| 依据规范 | `AGENTS.md` 全部规则、README.md 公共契约与编码规范 |
| 检验方法 | 源码逐项核对（git diff 全量审查）+ 双架构编译与测试实测 |

---

## 2. 命名规则严格检验

依据 AGENTS：**PascalCase 类名/函数、camelCase 局部变量、`m_` 成员、中文注释、`enum class`、`nullptr`、RAII、STL、避免裸 new/delete**。

### 2.1 新增标识符逐项核对（Core/Floor）

| 标识符 | 类型 | 规范要求 | 实际 | 判定 |
| --- | --- | --- | --- | --- |
| `EnqueueBatch` | 公有方法 | PascalCase | 匹配 | ✅ |
| `Contains` | 公有方法 | PascalCase | 匹配 | ✅ |
| `ids` | 参数 | camelCase | 匹配 | ✅ |
| `direction` | 参数 | camelCase | 匹配 | ✅ |
| `id` | 循环变量 | camelCase | 匹配 | ✅ |
| `seen` | 局部变量 | camelCase | 匹配 | ✅ |
| `queue` | 局部变量 | camelCase | 匹配 | ✅ |
| `m_upWaitingPassengers` / `m_downWaitingPassengers` | 既有成员 | `m_` 前缀 | 新增代码仅复用，未新增成员 | ✅ |
| 新增成员变量 | — | `m_` 前缀 | 无新增（0 个） | ✅ N/A |

### 2.2 新增标识符逐项核对（Tests/FloorTests.cpp）

| 标识符 | 类型 | 判定 |
| --- | --- | --- |
| `tests`、`floor`、`up`、`snapshot` | 局部变量 camelCase | ✅ |
| `TestSuite`、`Run`、`Check`、`Finish` | 既有框架 API | ✅ |

### 2.3 代码风格核对

| 检查项 | 判定 |
| --- | --- |
| 中文注释（新增 3 处注释均为中文） | ✅ |
| `enum class`（Direction::Up/Down 用法） | ✅ |
| STL 容器（`vector`、`deque`、`unordered_set`、`find`） | ✅ |
| 无裸 `new`/`delete`、无 C 数组动态数据 | ✅ |
| `nullptr` 使用 | ✅（`GetWaitingIds` 抛出 `invalid_argument` 而非返回裸指针，沿用既有契约） |
| `noexcept` 标注（`Contains` 为只读且不抛） | ✅ 与既有 getter 风格一致 |

**命名规则结论：全部通过，无违规。**

---

## 3. 接口情况严格检验

### 3.1 既有接口完整性（git diff 实证）

- `Core/Floor.h/.cpp` diff 为 **31 行纯新增、0 行删除**，以下既有公共接口逐字未改：
  `GetFloorNumber`、`GetUpWaitingCount`、`GetDownWaitingCount`、`GetSnapshot`、`Enqueue`、`RemoveFront`、`Peek`、`GetWaitingIds`。
- `Core/Passenger.*` **零改动**（9 个 getter、`MarkBoarded`/`MarkArrived`、`GetSnapshot`、构造函数全部保持原样）。
- 唯一公共契约 `Core/CommonTypes.h` **零改动**，未重复定义任何枚举/结构/配置类型。

### 3.2 新增接口

| 接口 | 签名 | 语义 | 无副作用 | 快照/副本 |
| --- | --- | --- | --- | --- |
| `EnqueueBatch` | `bool(ids, direction)` | 原子整组 FIFO 入队 | ✅ 仅修改本层队列 | ✅ |
| `Contains` | `bool(id) noexcept` | 去重/一致性校验 | ✅ 只读 | ✅ |

### 3.3 调用点核查

- 新增接口当前无外部调用（`rg` 全库确认），`Contains` 仅被 `EnqueueBatch` 内部使用——属**纯增量能力**，不影响任何既有调用路径。
- 未新增快照类型、未暴露可写容器出口、未通过 `friend`/可写指针绕过模块接口。

**接口情况结论：既有接口零破坏，新增接口为纯增量且语义明确。**

---

## 4. 公约（AGENTS.md）合规检验

| 编号 | 公约条款 | 判定 | 说明 |
| --- | --- | --- | --- |
| 1 | 优先保证 `ElevatorSimulation.sln` 可编译 | ✅ | 四配置 + 变更后重建均 0 错误 |
| 2 | Core 不包含 `pch.h`/`framework.h`/`afx*.h`/窗口头 | ✅ | 仅含 `CommonTypes.h` 与标准库 |
| 3 | `CommonTypes.h` 唯一公共定义 | ✅ | 未重复定义类型 |
| 4 | 楼层编号 1~L | ✅ | `Floor` 构造仍校验 `floorNumber>=1`；新增方法不引入楼层语义 |
| 5 | 初始化 N 分组、所有权规则 | ✅ | 未触碰初始化/所有权逻辑 |
| 6 | UI 只调公开接口、读快照副本 | ✅ | 未触碰 UI 与快照契约 |
| 7 | 修改尽量限于负责模块 | ✅ | 仅 Floor + 用户明确授权的 Tests/登记 |
| 8 | 不重构 MFC 自动生成代码 | ✅ | 未触碰 App/资源/PCH/framework |
| 9 | 新增核心 .cpp 需登记并禁用 PCH | ✅ N/A | 未新增核心 .cpp；新增测试 .cpp 按既有模式登记为 `None`+Development，不编入 MFC exe |
| 10 | 新文件 UTF-8、`/utf-8` | ✅ | 测试脚本已带 `/utf-8`；未动 `.rc` |
| 11 | PascalCase/camelCase/`m_`/中文注释/RAII/STL/`enum class`/`nullptr`/无裸 new/delete | ✅ | 见第 2 节逐项核对 |
| 12 | 公共接口修改前检查全部调用点、更新 README | ⚠️ **偏差** | 已核查调用点（无既有调用），但 README 接口表未同步更新（此前受"仅改 Floor/Passenger"约束，需授权补） |
| 13 | 未实现部分明确 TODO，不虚报占位/零统计 | ✅ | 功能真实实现并经测试；未宣称批量输入已接入 Simulation |
| 14 | 原 406 项冒烟检查不得删除 | ✅ | 冒烟仍为 406 项 × 双架构通过 |
| 15 | 模块职责边界（Tests 属 F） | ⚠️ **说明** | 本次新增测试属 F 范围，但由用户明确授权执行，不构成越权 |

**公约合规结论：14/15 条通过；2 项需注明——README 未更新（待授权补），Tests 修改（已获授权）。无硬性违规。**

---

## 5. 测试执行结果（功能证据）

环境：MSVC v143、`/std:c++17 /W4 /WX /utf-8 /MDd`，x64/x86 双架构，独立编译 Core/Statistics、无 MFC。

| 套件 | x64 | x86 |
| --- | --- | --- |
| **Floor（新增）** | 10 场景 / 35 断言 / 0 失败 | 10 场景 / 35 断言 / 0 失败 |
| Dispatcher | 83 / 444 / 0 失败 | 83 / 444 / 0 失败 |
| Elevator | 26 / 87 / 0 失败 | 26 / 87 / 0 失败 |
| Simulation | 41 / 2149 / 0 失败 | 41 / 2149 / 0 失败 |
| 冒烟基线 | 406 项通过 | 406 项通过 |

**本次复验（2026-09-02）**：`RunCoreTests.cmd Floor x64/x86` → 均 10 场景 / 35 断言 / 0 失败；`RunCoreSmokeTests.cmd [x86]` → 均 406 项通过。结果与上次全量一致。

Floor 套件覆盖：FIFO 顺序、方向独立、批内/跨方向去重原子性、非法参数、空批、追加顺序、`Contains` 生命周期、快照一致性。

---

## 6. README A 模块状态声明核验

README 状态行：`| A | Core/Passenger.*、Core/Floor.* | 已补齐状态转换、FIFO 与 ID 校验；后续可扩展交通输入方式 |`

| 声明项 | 代码依据 | 测试证据 | 判定 |
| --- | --- | --- | --- |
| 状态转换 | [Passenger.cpp](file:///e:/360MoveData/Users/sun/Documents/GitHub/ElevatorSimulation/ElevatorSimulation/Core/Passenger.cpp)：`MarkBoarded`(Waiting→Riding)、`MarkArrived`(Riding→Arrived)，非法转换返回 false | Smoke：初始状态 Waiting、时间戳 UnsetTime、同层乘客抛 `invalid_argument`；SimulationTests：上梯 T 完成变 Riding、下梯完成 Arrived 并删除活动对象 | ✅ 已补齐 |
| FIFO | [Floor.cpp](file:///e:/360MoveData/Users/sun/Documents/GitHub/ElevatorSimulation/ElevatorSimulation/Core/Floor.cpp)：上下行 `deque`，`Enqueue` 追加队尾、`RemoveFront` 弹出队头、`Peek` 返回队头 | FloorTests F2/F8：FIFO 顺序、多批追加保持全局顺序；SimulationTests：最老优先上梯 | ✅ 已补齐 |
| ID 校验 | Passenger 构造校验 `id>=0`、楼层合法、起终点不同、`requestTime` 有限；`Floor::Enqueue` 拒绝 `id<0` 与重复 ID | Smoke：同层/非法输入拒绝；FloorTests F4/F5/F6：批内重复、已在等待、负数均整批拒绝 | ✅ 已补齐 |
| 后续可扩展交通输入方式 | 已交付 `EnqueueBatch`（原子整组 FIFO 入队）+ `Contains`（去重/一致性校验） | FloorTests 10 场景 / 35 断言，双架构 0 失败 | ✅ **已扩展完成**（原"后续可扩展"现已实现） |

**核验结论：README 前四项声明（状态转换、FIFO、ID 校验）全部属实；"后续可扩展交通输入方式"已由本轮交付完成。README 状态行建议更新为「已补齐状态转换、FIFO 与 ID 校验；已扩展批量交通输入」。**

---

## 7. 结论

| 检查维度 | 结论 |
| --- | --- |
| 命名规则 | ✅ 全部通过，无违规 |
| 接口情况 | ✅ 既有接口零破坏；新增接口纯增量、语义明确 |
| 公约合规 | ✅ 14/15 通过；2 项需注明（README 未更新——待授权；Tests 修改——已授权） |
| 功能验证 | ✅ 双架构全量通过（160 场景/2715 断言 + 406 冒烟项）；本次复验一致 |
| README 状态核验 | ✅ 状态转换、FIFO、ID 校验声明属实；"后续可扩展交通输入方式"已实现完成 |

**需改进项**：README 接口行为表尚未补充 `EnqueueBatch`/`Contains`，且 A 模块状态行仍标注"后续可扩展交通输入方式"（建议改为「已补齐状态转换、FIFO 与 ID 校验；已扩展批量交通输入」）。该项为文档同步，建议授权后更新 README。
