# 跨端设计文档

本目录存放 Godot 客户端与 C++ 服务端共同遵守的行为设计。它解释状态归属、同步流程、性能目标和集成边界，供不同任务或 agent 并行开发时共享。

## 文档边界

- [`synchronization-model.md`](synchronization-model.md)：输入、固定 Tick、快照、插值和预测的时序模型。
- [`client-server-contract.md`](client-server-contract.md)：客户端与服务端的权威边界和阶段 2 集成契约。
- [`performance-budget.md`](performance-budget.md)：`30/64/128 Hz` 运行档、Tick 预算和观测指标。

精确的包字段、字节序、消息类型和测试向量只在 [`shared/protocol/`](../../shared/protocol/) 维护。设计文档不能悄悄改变协议；任何协议变更都必须同时更新规范、测试向量和相关设计说明。

架构取舍记录放在未来的 `docs/decisions/`。在确实有需要记录且会影响后续实现的决策前，不预先创建空白 ADR。
