# Buddy Robot 通信协议（ROS 2）

版本: v2.0  
日期: 2026-04-30  
状态: 当前有效

## 1. 范围

本规范定义仓库内 ROS 2 模块之间的通信约定，不再使用旧版单体 EventBus JSON 协议。

## 2. 消息与服务定义

自定义接口位于：`src/buddy_robot/buddy_interfaces`

- 消息：`UserInput.msg`、`CloudChunk.msg`、`Sentence.msg`、`ExpressionResult.msg`、`DisplayCommand.msg`
- 服务：`CaptureImage.srv`

修改接口时必须同步：

1. 更新 `.msg` / `.srv` 文件
2. 重新 `colcon build`
3. 更新依赖这些接口的包测试

## 3. Topic/Service 约束

1. Topic 命名保持“模块前缀 + 语义名”，例如 `/audio/wake_word`、`/dialog/sentence`。
2. 服务用于显式请求-响应场景，事件流优先 Topic。
3. 新增字段必须向后兼容，消费者可忽略未知字段。

## 4. 时序原则

1. 状态编排由 `buddy_state_machine` 负责。
2. 云端增量文本由 `buddy_cloud` 发布，`buddy_sentence` 负责切句。
3. `buddy_audio` 发送播放完成信号，驱动状态推进。
