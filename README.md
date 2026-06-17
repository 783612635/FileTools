# FileTools v0.1

高性能 Windows 文件批量管理工具 / High-performance Windows batch file management tool

Built with Dear ImGui (docking) + DirectX 11

---

## 编译环境 / Build Environment

| 项目 | 要求 |
|---|---|
| 操作系统 | Windows 10/11 (64-bit) |
| 编译器 | MSVC 2022 (Visual Studio 17.x) |
| CMake | ≥ 3.20 |
| C++ 标准 | C++17 |
| 图形 API | DirectX 11 |
| 字体 | 微软雅黑 + Segoe UI + Segoe MDL2 Assets（系统自带） |

### 依赖库 / Dependencies

| 库 | 版本 | 用途 |
|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | docking 分支 | GUI 框架 |
| [spdlog](https://github.com/gabime/spdlog) | v1.15.2 | 日志 |

CMake 通过 `FetchContent` 自动下载，无需手动安装。
Fetched automatically by CMake — no manual install needed.

### 编译命令 / Build Commands

```bash
cmake -B build -S .
cmake --build build --config Release
```

编译产物：`build/bin/Release/FileTools.exe`

---

## 运行要求 / Runtime Requirements

- **7za.exe**：需要 [7-Zip Extra](https://www.7-zip.org/download.html) 的独立控制台版本，放置于 `tools/` 目录（CMake 构建时会自动复制到输出目录）。
  The standalone console version of 7-Zip Extra is required. Place `7za.exe` (and optionally `7z.dll`) in the `tools/` directory — CMake will copy them to the output folder during build.
- Windows 10/11 系统字体（微软雅黑、Segoe UI、Segoe MDL2 Assets）
- DirectX 11 兼容显卡

---

## 功能 / Features

### 文件浏览器 / File Browser（右侧面板 / right panel）
- 浏览所有磁盘和文件夹 / Browse all drives and folders
- 快速访问：桌面、文档、下载等 / Quick access: Desktop, Documents, Downloads, etc.
- 显示文件大小、修改时间 / Display file size and modification time
- 右键菜单：添加到操作列表 / Right-click menu: Add to operation list
- Ctrl / Shift 多选 / Multi-select with Ctrl / Shift

### 操作列表 / Operation List（左侧面板 / left panel）
将文件从右侧浏览器添加到操作列表后，可执行以下批量操作：

#### 1. 修改后缀名 / Rename Extension
- 批量替换文件扩展名 / Batch replace file extensions
- 例 / e.g. `.txt` → `.md`

#### 2. 删除文件名字符 / Trim Characters
- 从文件名前面或后面删除指定数量的字符 / Remove N characters from the beginning or end of filenames
- 扩展名不受影响 / Extensions are preserved

#### 3. 移动到文件夹 / Move to Folder
- 批量移动文件到指定目录 / Batch move files to a target folder
- 支持浏览选择目标文件夹 / Browse-for-folder support
- 重名自动添加 `_(N)` 后缀 / Auto-append `_(N)` suffix on name conflict

#### 4. 解压 / Extract
- **自动识别**列表中的压缩文件（.7z .zip .rar .tar .gz 等）
- **没有压缩文件时，解压区域完全不显示**
- 解压到压缩文件所在目录的同名文件夹
  Extract to `<archive_dir>/<archive_name>/`
- 例 / e.g. `D:\data\backup.7z` → `D:\data\backup\`
- 异步执行，不阻塞 UI / Async execution, UI stays responsive

#### 5. 压缩为 7z / Compress to 7z
- 每个文件/文件夹单独压缩为同名 `.7z`
- 输出到原文件所在目录 / Output to the same directory as the source
- 异步执行，不阻塞 UI / Async execution

### 选择规则 / Selection Rule
- **不选 = 全部处理**：未选择任何文件时，操作应用于列表中所有文件
- **选了 = 只处理选中的**：选择文件后，操作仅应用于选中的文件
- 界面顶部有醒目提示，每个按钮悬停也有 tooltip 说明

  **No selection = process ALL.** When nothing is selected, operations apply to every file in the list.
  **Selection = filter.** When files are selected, only those files are processed.

---

## 注意事项 / Notes

1. **7za.exe 来源**：请从 [7-Zip 官网](https://www.7-zip.org/download.html) 下载 "7-Zip Extra: standalone console version"，将 `7za.exe` 放入 `tools/` 目录。首次 CMake 构建时会尝试自动下载，但不保证成功。
   Download `7za.exe` from the official 7-Zip website and place it in `tools/`.

2. **系统托盘**：关闭窗口会最小化到托盘，右键托盘图标可退出。 / Closing the window minimizes to the system tray; right-click the tray icon to exit.

3. **日志文件**：运行日志位于 `logs/FileTools.log`（与 exe 同目录）。 / Runtime logs are written to `logs/FileTools.log`.

4. **配置文件**：外观设置保存在 `FileTools.ini`（与 exe 同目录）。 / Appearance settings are saved to `FileTools.ini`.

5. **自定义背景色**：设置 → 客户区背景色，标题栏颜色会自动适配对比度。 / Settings → client area background color; the title bar color auto-adjusts for contrast.

6. **已知问题**：退出时可能触发 D3D11 清理异常（不影响功能使用）。 / Known issue: D3D11 cleanup exception on exit (does not affect functionality).
