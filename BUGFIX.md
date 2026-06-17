# Bug 修复说明 — FileTools 压缩/改名功能

## Bug 1：压缩按钮"未选择时不处理全部文件"

### 现象
操作列表中有文件，不选中任何文件，点击"压缩为 7z"没有任何反应。

### 根因：C++ dangling else

```cpp
// ❌ 原始代码
if (!m_selectedIndices.empty())
    for (auto idx : m_selectedIndices)
        if (idx < m_files.size()) targets.push_back(m_files[idx]);
else                          // ← 绑定到内层 if，不是外层 if！
    targets = m_files;        //    当未选择时永远执行不到
```

在 C++ 中，`else` 绑定到最近的未匹配 `if`，也就是 `if (idx < m_files.size())`。当 `m_selectedIndices` 为空时，外层 `if` 为 false，跳过整个 for 循环，`else` 也跟着被跳过了——`targets = m_files` 从未执行，结果 targets 始终为空。

### 修复

```cpp
// ✅ 加花括号明确绑定关系
if (!m_selectedIndices.empty())
{
    for (auto idx : m_selectedIndices)
        if (idx < m_files.size()) targets.push_back(m_files[idx]);
}
else
{
    targets = m_files;
}
```

### 为什么其他功能没这问题？
改名、删字符、移动、解压的同类代码都有花括号，唯独压缩按钮漏掉了。

---

## Bug 2：改名时程序崩溃

### 现象
点击"执行重命名"后程序卡死约 1 秒，随后崩溃。

### 根因：异常安全缺失 + `fs::equivalent` 抛异常

```cpp
// ❌ 原始代码 — 大量操作在 try-catch 之外
for (const auto& src : files)
{
    OpResult r;
    // ⚠️ 以下全在 try 外面，任何抛异常都会导致崩溃
    if (!fs::exists(src)) { ... }
    fs::path parent = src.parent_path();
    fs::path stem   = src.stem();
    fs::path dest   = parent / (newStem + newExtension);

    try
    {
        if (fs::equivalent(src, dest))  // ← 目标文件不存在时抛异常！
        { ... }
        fs::rename(src, dest, ec);
    }
    catch (const std::exception& e) { ... }  // ← 只覆盖了 try 块
}
```

关键问题：
1. `fs::equivalent(a, b)` 在**任一文件不存在时抛出 `filesystem_error`**。改名时目标路径通常不存在，必抛异常。
2. `fs::exists` 和路径拼接操作也在 try 之外，任何异常都会逃逸到 ImGui 帧循环导致崩溃。

### 修复

```cpp
// ✅ 整个循环体包在 try-catch 中 + 使用不抛异常的 equivalent
for (const auto& src : files)
{
    OpResult r;
    try
    {
        if (!fs::exists(src)) { ... }
        fs::path dest = ...;

        // 使用 error_code 重载，不抛异常
        std::error_code ecEq;
        if (fs::equivalent(src, dest, ecEq) && !ecEq) { ... }

        fs::rename(src, dest, ec);
    }
    catch (const std::exception& e) { ... }
    catch (...) { ... }              // ← 也捕获非标准异常
}
```

---

## UI 改进（顺带）

| 改动 | 位置 |
|---|---|
| 文件列表悬停气泡提示 Ctrl/Shift 多选 | `RenderFileList()` |
| 操作范围指示器（黄色=仅选中 / 灰色=全部） | `Render()` |
| 每个操作按钮动态 tooltip（"仅XX选中" / "XX全部"） | 各 `Render*()` 函数 |
| 提示文字从 "Ctrl/Shift 多选" → "不选=全部处理" | `RenderFileList()` |
