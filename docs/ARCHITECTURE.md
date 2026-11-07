# CopyMath 架构说明

> 面向要改这份代码的人。讲清楚：东西在哪、谁调谁、一次渲染怎么走完、
> 以及那些**踩过一次就不想再踩**的坑。

---

## 1. 它是什么

一个 Win32 小工具：左边写 LaTeX 公式，右边预览，一键复制/保存成图片。

**它自己不做排版**——把公式拼成一份 `.tex`，交给本机的 TeX Live（`pdflatex` + `pdftocairo`）
出图，再读进 GDI+ 显示/复制/保存。所以排版质量和论文里完全一致，代价是运行时要装 TeX Live。

技术栈：C++17 + Win32 API + GDI+ + Win32 资源（图标/版本信息）。不依赖任何第三方 GUI 库。

---

## 2. 分层与依赖方向

依赖只能往下，不能往上或横着乱连：

```
        ┌──────────────────────────────────────────────┐
        │  src/main.cpp          WinMain：DPI 感知、   │
        │                        初始化、消息循环       │
        └───────────────────┬──────────────────────────┘
                            │
        ┌───────────────────▼──────────────────────────┐
        │  src/ui/    主窗口 / 设置对话框 / 预览窗口 /  │
        │             编辑框子类化                      │
        └───────────────────┬──────────────────────────┘
                            │
        ┌───────────────────▼──────────────────────────┐
        │  src/app/    app.h（全局状态 + 接口声明）     │
        │              app_core.cpp（全局变量定义、小工具）│
        │              actions.cpp（复制/保存/重置/探测）│
        │              render_controller.cpp（线程调度）│
        └───────────────────┬──────────────────────────┘
                            │
   ┌────────────┬───────────┴────────┬─────────────┬──────────────┐
   ▼            ▼                    ▼             ▼              ▼
 src/render   src/config          src/log     src/clipboard   src/util
 (调 TeX)     (读写 cfg)          (日志)       (CF_DIB)        (编码/路径)
```

**规则**：最底下一排（render / config / log / clipboard / util）**不认识界面、不碰全局变量**，
只吃参数、返回结果 —— 所以能单独编译、单独测试（`tests/` 就是直接链 `util.cpp`）。

**唯一的例外**：`app/app.h` 把全局句柄和 `Config g_cfg` 暴露给所有人。
这是 Win32 小程序的务实取舍（省掉一整套依赖注入），代价是**谁都能改全局**，
所以约定：除了 `app_core.cpp`、`main_window.cpp`、`settings_dialog.cpp`，别的地方不要写 `g_cfg`；
动 `g_cfg` 一律先进临界区 `g_cs`。

---

## 3. 目录职责

| 路径 | 放什么 |
|---|---|
| `src/main.cpp` | 程序入口。只做：声明 Per-Monitor V2 DPI 感知 → `CoInitializeEx` / GDI+ / 载入配置 / 建窗口类 → 建主窗口 → 消息循环（含加速键表） |
| `src/app/app.h` | 全局状态的外部声明、控件 ID、自定义消息、所有跨模块函数原型 |
| `src/app/app_core.cpp` | 全局变量的**唯一定义处**；`Px()`（96DPI→当前 DPI）、`SetStatus()`、`CurrentDpi()`、`ApplyTopmost()` |
| `src/app/actions.cpp` | 用户动作：复制到剪贴板、保存 PNG、重置、自动探测 TeX Live、选文件夹；日志封装 `LogUsage/LogSuffix` |
| `src/app/render_controller.cpp` | 渲染调度：版本号 `g_gen` + 忙标志 `g_rendering` + 后台线程 `RenderThread` |
| `src/ui/main_window.cpp` | 主窗口过程：建控件、`WM_SIZE` 手写布局（按钮自动换行）、命令分发、接收线程结果 |
| `src/ui/settings_dialog.cpp` | 设置对话框：六项设置、校验、保存 |
| `src/ui/preview_view.cpp` | 右侧预览窗口自绘：白底居中显示图；出错时红字画 LaTeX 报文 |
| `src/ui/edit_subclass.cpp` | 编辑框子类化：Ctrl+A 全选；粘贴时把裸 `\n` 补成 CRLF |
| `src/render/` | 拼 `.tex` → `pdflatex` → `pdftocairo -r <DPI>` → GDI+ 读入 → 按墨迹二次裁剪 |
| `src/config/` | `CopyMath.cfg`（UTF-8，自定义 `KEY=VALUE` + `PREAMBLE_START/END` 夹块）的读写；`kDefaultPreamble` |
| `src/log/` | 可选的 `logs.txt`：追加时间 / 操作 / DPI / 尺寸 / 源码 |
| `src/clipboard/` | 把位图以 `CF_DIB` 写进剪贴板，并写 `biXPelsPerMeter`（Word 靠它算物理尺寸） |
| `src/util/` | UTF-16↔UTF-8、`ToCrlf/ToLf`、`EnsureDirectory`、`DirNameOf`、`ExeDir/AbsFromExeDir` |
| `assets/` | **构建输入**：`CopyMath.ico`（嵌进 exe）、`CopyMath.png`（README 用） |
| `src/app.rc` | 图标 + 版本信息（`VERSIONINFO`），由 CMake 交给 `windres -c 65001` 编译 |
| `tests/` | 纯逻辑单测（目前覆盖 util 的换行/路径函数） |
| `bin\` | 成品输出目录：`CopyMath.exe` + `CopyMath.cfg` + 运行时 `tmp\` / `export\` |
| `build/` | CMake 生成的中间文件（**不入库**） |

---

## 4. 一次渲染的完整路径

```
用户按 F5 / 点“渲染”
   │
   ▼  RenderNow()                       app/render_controller.cpp
   ├─ ResetRenderState()   清 tmp/formula.*、丢掉旧位图、g_gen++
   ├─ SetStatus("正在渲染…")
   └─ RequestRender()
        ├─ g_gen++，g_currentTex = 输入框内容
        └─ 若没有在跑的线程 → CreateThread(RenderThread)
                                  │
                                  ▼  RenderThread（后台）
                                  Loop:
                                    ├─ 快照 {gen, tex, preamble, texbin, dpi}
                                    ├─ RenderFormula(...)      render/render.cpp
                                    │    ├─ StripDelims / 判断是否外层环境 / 拼 mathBlock
                                    │    ├─ 组装 .tex（导言区 + preview 宏包）
                                    │    ├─ 写 <项目目录>\tmp\formula.tex
                                    │    ├─ pdflatex -interaction=nonstopmode -halt-on-error
                                    │    ├─ pdftocairo -png -r <DPI> -singlefile
                                    │    ├─ GDI+ 读 PNG → TrimToInk 按墨迹裁边
                                    │    └─ 删中间文件，返回 Bitmap*
                                    ├─ 版本号变了？ → delete 掉结果，continue 重来
                                    └─ PostMessage(WM_RENDER_DONE / WM_RENDER_ERROR)
   ▼  主线程收到消息
   ├─ WM_RENDER_DONE  → 换上 g_viewBitmap、刷新预览、状态栏报尺寸与 DPI
   └─ WM_RENDER_ERROR → 红字显示 LaTeX 报文
```

**为什么用版本号 + 忙标志**：渲染一次要 1～3 秒，期间用户可能又改了输入。
用 `g_gen` 判断“结果是否过期”，用 `g_rendering` 保证同一时刻只有一个 `pdflatex`
在抢同一批临时文件。两者的判断与清除必须在**同一个临界区**里完成，否则会漏渲染
（详见 `render_controller.cpp` 顶部注释）。

---

## 5. 全局状态与线程安全

| 变量 | 含义 | 谁改 |
|---|---|---|
| `g_cfg` | 全部设置 | 设置对话框保存时、`ApplyTopmost`、启动载入 |
| `g_gen` / `g_rendering` / `g_currentTex` | 渲染请求的版本号 / 忙标志 / 源码快照 | `RequestRender`、`RenderThread` |
| `g_viewBitmap` | 当前显示的公式图 | 主线程独占（`WM_RENDER_DONE` 里换、`WM_DESTROY` 里删） |
| `g_hMain` 等 `HWND` | 控件句柄 | 只在 `WM_CREATE` 里赋值 |

临界区 `g_cs` 只保护**上面表格里跨线程的那几项**。UI 句柄不需要保护
（只在主线程访问）。别把耗时操作放进临界区。

---

## 6. 关键实现细节（都是踩过的坑）

### 6.1 多行 EDIT 只认 CRLF —— v1.3 的核心修复

- 配置 / 内存里统一用 `\n`；**送进 EDIT 前必须 `ToCrlf()`，取回后 `ToLf()`**。
  直接 `SetWindowTextW` 塞 `\n` 文本，控件会把整段当成一行（换行处显示成方块）。
- 多行 EDIT 必须带 **`ES_WANTRETURN`**：本程序的窗口是 `CreateWindowEx` 建的普通窗口，
  不是对话框；缺这个样式时回车会被当成“按默认按钮”，不插入换行。
- 粘贴也走这条规则：`EditProc` 拦 `WM_PASTE`，剪贴板里若是“裸 `\n`”（网页/PDF 常见）
  就用 `EM_REPLACESEL` 补成 CRLF；否则交回默认处理，不影响其它剪贴板格式。

### 6.2 导言区必须以换行结尾

拼 `.tex` 时用户导言区后面紧跟 `\usepackage[active,tightpage]{preview}`。
如果导言区最后一行是 `%` 注释，没有换行就会把后面那行一起注释掉 → 渲染直接失败。
`render.cpp` 里会强制补一个 `\n`。

### 6.3 出图为什么要裁两次

`preview` 宏包的 `tightpage` 只保证**高度**方向贴边。宽度方向：
公式用内联 `$\displaystyle ...$`（盒子宽度 = 公式自然宽度），而 `align` / `equation`
这类外层环境无法内联，盒子宽度仍是一整行（`\hsize`），两侧会留大片空白。
所以出图后再按非白像素的包围盒裁一次，四周留按 DPI 折算的窄边（300 DPI → 3px）。

### 6.4 GDI+ / 剪贴板

- `DIB` 头里的 `biXPelsPerMeter` 一定要按设置里的 DPI 写，否则 Word 会按 96 DPI 换算物理尺寸，
  300 DPI 的图粘进去显得特别大。
- 保存 PNG 前 `Bitmap::SetResolution()`，让编码器写出正确的 `pHYs`。

### 6.5 便携路径

- `ExeDir()` 取 exe 所在目录；`CopyMath.cfg`、`tmp\`、默认导出目录 `export\` 全都相对它。
- 配置里的路径允许相对写法，`AbsFromExeDir()` 统一展开 —— 整个文件夹搬走仍然有效。
- 渲染中间文件放在 `<exe目录>\tmp\`（不是 `%TEMP%`）：渲染完即删，退出时清空并删目录。
  只有该目录不可写时才退回 `%TEMP%\CopyMath`。
- 外部依赖 `pdflatex.exe` / `pdftocairo.exe` 只能来自本机 TeX Live，配置里存绝对路径。

### 6.6 构建侧

- `app.rc` 是 UTF-8，`windres` 必须带 `-c 65001`，否则中文版本信息变乱码；
  图标路径是相对**项目根**的 `assets/CopyMath.ico`，靠 CMake 传的 `-I` 找到。
- `.bat` 文件必须是 **ASCII + CRLF**：中文在 GBK 控制台下乱码，LF 换行会让 `^` 续行解析出错。

---

## 7. 构建、测试、运行

```bat
:: 一键（等价于下面三条）
build.bat

:: 手动
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

产物输出到 `bin\`：`CopyMath.exe` 和 `CopyMath.cfg` 并排，程序运行时读写的 `tmp\` / `export\` 也在这里；编译中间文件全在 `build/`。

运行前置条件：本机装有 TeX Live（需要 `pdflatex.exe` 与 `pdftocairo.exe`），
路径在「设置 → TeX Live 的 bin 目录」里指定。

---

## 8. 改版本号的清单

三处必须一起改，否则标题 / 属性 / 构建脚本会互相打脸：

1. `CMakeLists.txt` 的 `project(CopyMath VERSION x.y.z ...)`
2. `src/app.rc` 的 `FILEVERSION` / `PRODUCTVERSION`（逗号分隔）与 `FileVersion` / `ProductVersion`（点分隔）
3. `src/app/app.h` 的 `COPYMATH_VERSION`（窗口标题用）

改完重编，在「右键 exe → 属性 → 详细信息」核对。

---

## 9. 已知限制

- 只用 `pdflatex`，中文支持有限（公式里夹少量中文渲染不了）。
- 渲染是**手动触发**（F5），不随输入实时更新 —— 每次都要起 `pdflatex` 进程，实时渲染会卡。
- 同一时刻只允许一个渲染线程；连续狂按 F5 只会重渲最新内容，不会排队。
- 没有多文档 / 历史记录界面（日志可选，只是纯文本追加）。
