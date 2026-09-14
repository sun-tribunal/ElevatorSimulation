# 项目开发规则

这是 Windows / Visual Studio 2022 / C++17 / MFC 的“多电梯群控调度仿真系统”。已实现课程设计核心仿真，正式动态 UI 待后续开发。

## 必须遵守

- 优先保证 `ElevatorSimulation.sln` 可编译，在现有唯一工程内增量开发。
- 核心逻辑与 MFC UI 解耦；Core/Statistics 不包含 `pch.h`、`framework.h`、`afx*.h` 或具体窗口头，不访问 MFC 控件。
- `ElevatorSimulation/Core/CommonTypes.h` 是唯一公共定义。禁止重新定义 Direction/PassengerState/ElevatorState/SimulationConfig 及其语义重复类型。
- 楼层编号为 1~L，禁止混用 0 起始楼层编号。电梯容器下标和 id 为 0~N-1，UI 显示 E1~EN。
- 初始化 N 为 3 的正倍数，前 N/3 位于 1 层、中 N/3 位于 L 层、后 N/3 位于 `(L+1)/2`，初始空梯/Idle。
- Simulation 唯一拥有乘客对象，Floor/Elevator 仅存 PassengerId；禁止重复释放、长期缓存易失效元素地址或用全局变量保存仿真。
- UI 只调用 Simulation 公开控制接口、读取快照副本。禁止可写容器出口、访问 private 数据或在 Dialog 内实现业务算法。
- 每次修改尽量限于负责模块。公共接口修改前检查所有调用位置、更新 README，并编译所有受影响配置。
- 不擅自大规模重构 MFC 自动生成代码；App、资源、PCH、framework 文件保留原位置和有效功能。
- 新增核心 `.cpp` 要同时加入 `.vcxproj` / `.filters`，对该文件禁用 PCH；保留 MFC 文件的 PCH 设置。
- 新增 C++ 文件 UTF-8，各配置 `/utf-8`；原 `.rc` 保留 UTF-16，不随意改变资源编码。
- PascalCase 类名/函数，camelCase 局部变量，`m_` 成员；中文注释，RAII、STL、enum class、nullptr，新增代码避免裸 new/delete。
- 用户已授权实现 Dispatcher/Elevator/Simulation，并对 Passenger/Floor/Statistics 作必要适配；不要把已实现功能退回占位，也不要擅自扩展正式动画或高级 AI 策略。
- Dispatcher 只读评分；顺路与空闲统一比较 Cost/ETA，非顺路忙碌附加 S+T 成本，不设绝对等级。按 Cost、ETA、距离、任务数、ID 排序；当前满载梯若预测在请求层接客前释放容量，可以参与候选；若到请求层完成下客后仍满载，则不分配。禁止直接操作 Elevator 的状态。
- ETA 与 Elevator LOOK 一致：所有前方内呼和双向外呼决定扫描/折返，只在内呼或同向外呼处服务。每个不同内呼最多估计下 1 人并消费一次；未知外呼固定计 T、有容量时最多估计上 1 人，不猜测目的层。Alighting 当前一人只计剩余时间并释放一席；Boarding 当前预留者只计一次且目的层在 Boarded 前隐藏。真实状态机仍逐人计 T。
- Simulation 知道真实乘客状态，Dispatcher 只用传统上下行按钮可观测信息。调度快照禁止携带等待目的层、FIFO 前缀、真实 waitingCount 或精确上下客人数；UI/统计可保留 waitingCount。FIFO 仅用于实际 Floor::Peek / RemoveFront 服务。满载时按接客前已知 Car Call 估计释放席位；LoadCost = T × 接客前估计载荷 / capacity。Joint/Deferred/Reassignment/FleetRebalancer/Coverage 统一复用 ScoreSnapshot，由真实事件重评估修正误差。
- Elevator 仅执行已接受任务，层间运动和上下客中不能因新请求反向。Advance 最多返回一个事件，调用方必须处理其余时间预算。
- 时间全部以仿真秒处理，只有 Simulation::Update 将真实秒乘一次 simulationSpeed。同步处理所有电梯事件，不按电梯逐台推进整帧。
- passengerRate 单位为全楼人数/仿真秒，Poisson 指数间隔。固定种子用于可重复测试；Reset 保留本轮 seed。
- Hall Call 由 Simulation 按(楼层,方向)唯一管理；满载剩余人数留队重分配。上梯 T 完成才出队，下梯 T 完成才从活动注册表删除。
- Hall Call 在新乘客、到层、上下客完成/状态变化等模型事件后重评估，不在纯帧边界评估。同一仿真时刻只改派一轮。先评分原梯；请求层真正 Stopped/Boarding/Alighting 且非 betweenFloors 时始终保护。原梯不可行时绕过普通临近锁、10 秒冷却和 5 秒收益阈值；仍可行时才应用这些滞回条件。临近锁仅保护 betweenFloors 且 Moving 方向朝向请求层、整数层距离 <=1 的原梯；同层已驶离或一层内正在远离不锁定。阈值统一在 Dispatcher.h，Simulation 同步两梯外呼及归属。
- Elevator::RemoveHallCall 只撤销指定方向外呼；当前层真正 Stopped/Boarding/Alighting 禁止，Moving 中 currentFloor 是已驶离整数层时允许。不能删除内呼或改变当前动作、方向、剩余时间。改派提交只对已选定两台梯准备副本后移动回写，不用于搜索试组合；既有 Elevator 状态机不得重构。
- 联合分配每批取最老 3 个 Active 未分配外呼，每层搜索最多 3 个候选梯 + 暂不分配，最多 64 个叶组合。只能在局部快照中逐项加任务，最终重算各请求 ETA/载荷。先最大化可分配数量，再按总 Cost、最大 ETA、总 ETA、请求顺序中的电梯 ID 稳定比较；方向惩罚沿用各请求插入前上下文，Aging 保留。允许同梯多请求和部分未分配。同一 DispatchCalls 内提交后重建快照并重新预筛下一批，直到无 Active pending 或本批 assignedCount=0；成功批次严格减少 pending，循环内不重复动态改派。
- 未分配外呼按 firstRequestTime / firstPassengerId / key 排序，并复用 ScoreSnapshot 预筛：存在可行候选为 Active，全部不可行为临时 DeferredCapacity，不占三个请求名额。Deferred 不保存为永久状态，不删 Floor/HallCall，不重置队头 ID 或等待时间；Alighted、改派/撤销/释放等容量或路线事件通过既有 m_dispatchDirty 重新评估，同一调度内路线变化则直接重建快照。禁止第二套容量逻辑、UI 每帧扫描或额外定时事件；不扩 UI 公共接口。
- 固定归属贪心基线为 0fade614d5095eb14b3cb63916af876f3d2e1aa3；Tests/RunDispatchComparison.ps1 在 build 内只读导出，使用同一客流/工具链比较。不得隐瞒退化场景、积压或计算开销，不声称全局最优。
- 截止前允许产生乘客，截止时完成的动作计入，但不推进到总时长之后。统计均值口径见 docs/AlgorithmDesign.md，不把未完成样本悄悄计入已完成均值。
- 未实现部分必须明确 TODO，不能将占位返回值或零统计声称为完整功能。
- 基础验证：原 `Tests/RunCoreSmokeTests.cmd` 的 406 项检查不得删除（默认 x64，也支持参数 x86）。运行 `Tests/RunCoreTests.cmd All x64`（及 x86）和双架构 Smoke，生成 Debug/Release × x64/x86 四配置。测试无需另建 VS 工程或安装库。

## 模块负责人

| 负责人 | 文件范围 |
| --- | --- |
| A | Core/Passenger.*、Core/Floor.* |
| B | Core/Elevator.* |
| C | Core/Dispatcher.*（类名 ElevatorDispatcher） |
| D | Core/Simulation.* |
| E | ElevatorSimulationDlg.* 与 UI 资源 |
| F | Statistics/*、Tests/* |

完整接口语义、所有权、时间单位和开发 TODO 见 README.md。

## Environment（保留用户提供的工具规则）

优先使用已有工具，不要重复安装。新增常用工具时更新本文件，记录名称、路径、用途。

| 工具 | 路径 / 地址 | 用途 |
| --- | --- | --- |
| Locale Emulator | `E:\Locale.Emulator.2.5.0.1\LEProc.exe` | 日文程序区域模拟 |
| ReverseTools | `C:\Users\20938\AppData\Local\Programs\ReverseTools` | 游戏逆向、资源分析和解包 |
| DirectXTex texconv | `C:\Users\20938\AppData\Local\Programs\ReverseTools\DirectXTex\texconv.exe` | DDS / 3Dmigoto 纹理转换 |
| Bandizip | 已安装，用户未指定路径 | 解压缩 |
| Ollama | `http://127.0.0.1:11434` | 本地模型服务 |
| Visual Studio 2022 Community | `C:\Program Files\Microsoft Visual Studio\2022\Community` | 本项目 MSVC / MFC / MSBuild 编译调试 |
| MSBuild | `C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe` | 生成现有解决方案 |
| Python 3.11 | `C:\Users\20938\AppData\Local\Programs\Python\Python311\python.exe` | 辅助文件检查与脚本，非运行依赖 |
| VS Code / Conda | 已安装，用户未指定路径 | 编辑与开发辅助 |

ReverseTools 已包含 AssetStudio、GARbro、Kuriimu2、QuickBMS、arc_unpacker、Resource Hacker、dnSpy、x64dbg、DirectXTex/texconv。本次未安装新工具。
