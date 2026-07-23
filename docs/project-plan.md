# 项目实施计划

## 项目目标

本项目首先是一个可运行、可测量的实时网络同步实验环境，而不是完整 MMORPG。Godot 客户端负责输入、表现和诊断；C++ 服务端负责固定 Tick 模拟、状态校验和权威结果。每个阶段都应能独立运行并产生可观察结果。

## 当前基线

- `client/` 是 Godot 4.7.1 2D 工程，启动场景为 `scenes/main.tscn`，显示本地实验状态和目标 Tick 频率。
- `server/` 已有 C++20 CMake 工程、固定 Tick 时钟、UDP `Hello/HelloAck/Ping/Pong` 服务端和不依赖第三方框架的单元测试。
- `shared/protocol/protocol.md` 已固定 v1 的 10 字节大端序包头和字节级测试向量。
- 本机已验证 CMake 4.2.1、Visual Studio 18 2026 和 MSVC 19.51 工具链。
- 当前没有数据库、账号系统、第三方网络库或测试框架。

## 已知问题

- 受限自动化环境直接无界面运行 Godot 场景可能在进入用户脚本前触发原生 `signal 11`；普通本机权限下的无界面编辑器加载已通过。该限制不影响当前客户端资源验证。

## 目录规划

```text
client/
  scenes/                 # 主场景、玩家与诊断界面
  scripts/
    network_client.gd     # UDP 收发与连接状态
    local_player.gd       # 输入与本地预测
    remote_player.gd      # 快照缓冲与插值
    diagnostics.gd        # RTT、丢包和误差显示
  assets/
server/
  CMakeLists.txt
  include/bored/          # 公共 C++ 接口
  src/                    # 网络循环、世界模拟和程序入口
  tests/                  # 协议、Tick 和世界逻辑测试
shared/protocol/
  protocol.md             # 与语言无关的包格式
  test_vectors/           # 跨语言编解码样本
tools/
  bots/                   # C++ 压测客户端
  network_proxy/          # 延迟、抖动、丢包与乱序模拟
docs/
```

上述文件按阶段创建，不提前建立没有实际职责的抽象层。

## 已确定的技术决策

- 客户端使用 Godot 和 GDScript，实时服务端使用 C++20 与 CMake。
- 客户端只发送输入意图；位置、技能结果和生命值以服务端为准。
- 第一版使用 UDP 和自有小型二进制协议，不接入 Godot 高级 RPC。
- 模拟 Tick 与快照发送频率分离；初始目标分别为 `30 Hz` 和 `15 Hz`。
- 网络收发不能阻塞模拟；跨线程通信必须有界并可统计积压。
- 第一阶段不引入 Protobuf、数据库、Redis、登录服、微服务或 GDExtension。

## 开发阶段

### 阶段 0：可构建基线（已完成）

建立 CMake 工程、服务端入口、固定 Tick 时钟和最小测试目标。客户端增加简单状态界面。

验收：Debug 构建和测试通过；服务端持续运行，Tick 漂移和耗时可打印；Godot 客户端可启动。

### 阶段 1：连接与协议（已完成）

定义包头、协议版本、消息类型、序号和长度校验，实现 `Hello`、`Ping`、`Pong`。Godot 使用底层 UDP API，服务端为客户端分配临时 ID。

验收：两个本地客户端能同时连接；界面显示连接状态和 RTT；畸形包不会导致服务端崩溃；C++ 编解码测试有固定测试向量。

### 阶段 2：权威移动

客户端发送带序号和 Tick 的移动输入。服务端在固定 Tick 中模拟玩家，并以独立频率广播世界快照。

验收：两个窗口能看到彼此移动；客户端修改位置数据不能绕过服务端；日志能统计输入延迟、包量和快照大小。

### 阶段 3：插值、预测与校正

远端玩家使用快照缓冲插值；本地玩家执行预测，并根据服务端确认重放未确认输入。实现可视化误差线和校正计数。

验收：在 `100 ms` 延迟、`20 ms` 抖动和 `5%` 丢包下仍可操作；关闭各算法时能直观看到差异；预测误差和快照年龄可测量。

### 阶段 4：战斗与延迟补偿

加入一个有冷却和命中范围的技能，记录有限历史状态，实验服务端回溯判定。

验收：技能结果完全由服务端决定；不同延迟客户端的命中判定可重放、可解释。

### 阶段 5：AOI 与压力测试

实现简单网格 AOI、C++ 机器人和带宽统计，逐步增加实体与连接数量。

验收：压测脚本可重复运行；输出 Tick P50/P95/P99、带宽、实体数和 AOI 广播量；性能退化点有记录。

### 阶段 6：平台服务

同步实验稳定后再增加登录、大厅、角色持久化和部署。届时单独评估 Go 平台服务，避免影响实时主线。

## 当前下一步

1. 定义带输入序号和客户端 Tick 的移动输入消息，并为其补充协议测试向量。
2. 在服务端固定 Tick 中维护玩家位置，以低于模拟频率的独立频率发送世界快照。
3. 在两个 Godot 客户端中显示权威位置，验证客户端不能直接改写服务端状态。

## 本机命令

```powershell
& "F:\godot\Godot_v4.7.1-stable_win64.exe" --editor --path client
& "F:\godot\Godot_v4.7.1-stable_win64_console.exe" --headless --editor --path client --quit

cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
.\server\build\msvc-debug\Debug\bored_server.exe --ticks 35
```

阶段 0 和阶段 1 已在 MSVC 下完成配置、构建、测试和本机 UDP `Hello/Ping/Pong` 往返验证。
