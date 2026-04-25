# 方案对比：自研模块系统 vs 精简 ROS2 Component

版本: v1.0
日期: 2026-04-26
用途: 未来方案迁移参考

---

## 1. 概念映射

| 自研概念 | ROS2 对应 | 说明 |
|----------|-----------|------|
| `IModule` | `rclcpp::Node` (Component) | 生命周期接口对等 |
| `CreateModule / DestroyModule` | `ComponentManager::load_component()` | ROS2 通过 `class_loader` 自动发现加载 `.so` |
| `ModuleRuntime` | `rclcpp::Container` + `Executor` | Container 管理组件生命周期，Executor 调度回调 |
| `EventBus` (pub/sub) | `rclcpp::Publisher / Subscription` | DDS 层通信，支持 intra-process 优化 |
| JSON 配置 + 拓扑排序 | `launch` 文件或 `ComponentManager` XML/YAML | ROS2 用 launch 系统编排启动顺序 |
| `Event` 结构体 | `.msg / .srv` IDL 定义 | 需要先定义消息类型，生成序列化代码 |
| `EventBus::Listener` RAII | `rclcpp::Subscription` RAII | 语义一致 |
| `ModuleContext` | `NodeOptions` + 参数服务 | 依赖注入 vs 声明式参数 |

---

## 2. 自研方案现状

核心代码量约 400 行，覆盖以下能力：

| 能力 | 实现 | 关键文件 |
|------|------|----------|
| 模块接口 | `IModule` — 6 个生命周期钩子 | `src/app/imodule.h` |
| 动态加载 | `dlopen / dlsym` + C 工厂函数 | `src/app/module_runtime.cpp` |
| 生命周期管理 | `ModuleRuntime` — JSON 配置 + 拓扑排序 | `src/app/module_runtime.cpp` |
| 进程内通信 | `EventBus` — topic pub/sub + RAII Listener | `src/core/event_bus.h` |
| 依赖排序 | Kahn 拓扑排序 | `src/app/module_runtime.cpp` |
| 模块注册 | `ModuleRegistry` — unordered_map | `src/app/module_registry.cpp` |

数据流：模块 Publish 事件 → EventBus 分发 → 其他模块 Listener 回调，全进程内，零序列化。

---

## 3. 逐维度对比

### 3.1 复杂度与依赖

| 维度 | 自研 | ROS2 精简版 |
|------|------|------------|
| 外部依赖 | 零（`dlfcn.h` + 标准库） | ROS2 核心 ~50MB（DDS + rclcpp + class_loader），交叉编译到 RK3588 需额外工作 |
| 代码量 | ~400 行（runtime + event_bus） | rclcpp headers 上万行，需理解 Node / Executor / Callback Group 体系 |
| 编译工具链 | CMake + Conan，现有流水线零改动 | 需要 `colcon build`、rosdep、IDL 代码生成步骤 |
| 学习曲线 | 已掌握 | 需学 launch、QoS、DDS、component 接口 |

### 3.2 性能（嵌入式场景关键）

| 维度 | 自研 | ROS2 |
|------|------|------|
| Intra-process 延迟 | 直接函数调用，纳秒级 | intra-process 优化后微秒级，仍经 DDS middleware |
| 内存零拷贝 | `shared_ptr<Event>` 天然共享 | 需 `unique_ptr` + `std::move` intra-process 模式 |
| 消息传递 | 直接传 C++ 结构体指针 | 必须通过 IDL 生成消息类型，有序列化开销 |
| 启动时间 | 毫秒级 | DDS 发现 + daemon，秒级 |
| 运行时常驻开销 | ~0（一个 mutex + hashmap） | DDS 线程池、发现协议、QoS 管理，10–30MB |

### 3.3 可扩展性

| 维度 | 自研 | ROS2 |
|------|------|------|
| 多进程 | 需自行加 IPC（架构文档 Section 10 已预留） | 天然支持，DDS 自动跨进程 |
| 多机分布式 | 不支持 | DDS 自动发现，天生分布式 |
| 调试工具 | GDB + spdlog | `rviz2`、`rqt`、`ros2 topic echo` 等生态工具 |
| 社区生态 | 无 | Nav2、Perception、SLAM 等大量现成包 |

### 3.4 开发效率

| 维度 | 自研 | ROS2 |
|------|------|------|
| 新增模块 | 继承 `IModule` + `CreateModule` + JSON 配置，约 5 分钟 | Component + `.msg` 定义 + `colcon build` + launch 文件，约 30 分钟+ |
| 新增事件类型 | `event.h` 加 struct，1 分钟 | `.msg` 文件 → 编译生成 → rebuild 依赖方，5 分钟+ |
| 调试 | 单进程 GDB 一行 attach | 多节点 + DDS 层，需 `ros2 doctor` 等工具排查 |
| 常见坑 | 自己的代码，可控 | DDS QoS 不匹配、消息类型版本不一致等问题较多 |

---

## 4. 结论

### 当前场景评分

```
自研方案适合度：  ★★★★★
ROS2 精简方案：   ★★☆☆☆
```

### 核心判断

1. **ROS2 核心价值（分布式、DDS 发现、多进程）当前用不到。** 架构文档明确主路径为单 App + 进程内通信。

2. **嵌入式性能代价显著。** RK3588 上 DDS 常驻开销挤占 NPU / Audio 资源，音频链路对延迟极度敏感。

3. **自研 400 行代码已覆盖全部需求。** `IModule + dlopen + EventBus` 是 ROS2 Component + Intra-process 的最小子集，无多余概念。

---

## 5. 何时考虑迁移到 ROS2

满足以下**任意两条**时可启动评估：

- [ ] 需要多机协作（多机器人编队）
- [ ] 需要接入 Nav2 / SLAM 等 ROS2 生态包
- [ ] 团队已有 ROS2 生产经验
- [ ] 模块数量增长到 20+ 且出现跨进程隔离需求
- [ ] 需要云端-边缘分布式部署

### 推荐迁移路径（混合方案）

```
buddy_app (自研框架不变)
    │
    ├── audio_pipeline_module
    ├── vision_pipeline_module
    ├── cloud_client_module
    ├── state_machine_module
    ├── dialog_manager_module
    ├── sentence_segmenter_module
    │
    └── ros2_bridge_module          ← 新增桥接模块
            │
            │  WebSocket / 共享内存
            │
        ros2_container              ← 独立进程
            ├── nav2_stack
            ├── slam_node
            └── ...
```

桥接模块职责：
- 单向或双向转发 EventBus 事件到 ROS2 topic
- 避免将 ROS2 依赖引入核心模块系统
- 按需启停，不影响现有模块

### 迁移工作量估算

| 步骤 | 工作内容 | 风险 |
|------|----------|------|
| 1. 交叉编译 ROS2 for RK3588 | aarch64 toolchain + DDS 配置 | 中 — FastRTPS / CycloneDDS 选型和调优 |
| 2. 定义消息 IDL | 将 `Event` 子类映射为 `.msg` | 低 — 机械性工作 |
| 3. 实现 ros2_bridge_module | 双向转发 + 生命周期映射 | 低 — 参考现有模块模式 |
| 4. 集成测试 | 延迟、内存、稳定性验证 | 高 — 需要在实板上长时间压测 |

---

## 6. 参考资料

- 项目架构文档: `docs/architecture.md`
- 通信协议: `docs/communication_protocol.md`
- ROS2 Component 文档: https://docs.ros.org/en/humble/Concepts/Intermediate/About-Composition.html
- ROS2 Intra-process: https://docs.ros.org/en/humble/Tutorials/Advanced/Intra-Process-Communication.html
