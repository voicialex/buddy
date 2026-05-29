"""Prompt templates for memory summarization and fact extraction."""

SUMMARIZE_PROMPT = """请用3-5句话总结以下对话的要点。
包括：用户的主要问题、得到的回答、情绪状态、未解决的事项。
只输出总结内容，不要输出任何其他内容。

对话：
{conversation}"""

EXTRACT_FACTS_PROMPT = """从以下对话中提取值得长期记住的用户信息。
输出JSON数组，每项包含 content（事实内容）和 category（分类）。
分类只能是以下三种之一：user_info（姓名年龄等个人信息）、preference（偏好习惯）、knowledge（领域知识）

已有记忆（避免重复，如有更新则用新内容覆盖旧内容）：
{existing_facts}

对话：
{conversation}

输出格式示例：[{{"content": "用户叫小明", "category": "user_info"}}]
如果没有值得记住的新信息，输出空数组：[]
只输出JSON，不要输出任何其他内容。"""

DEDUPE_FACTS_PROMPT = """对比新提取的事实和已有事实列表，合并去重。
规则：
- 如果新事实更新了旧事实的内容（如名字变了、偏好变了），用新的替换旧的
- 如果新事实和旧事实完全相同，保留旧的
- 如果新事实是全新的信息，添加到列表中

已有事实：
{old_facts}

新提取的事实：
{new_facts}

输出合并后的完整JSON数组，格式：[{{"content": "...", "category": "..."}}]
只输出JSON，不要输出任何其他内容。"""