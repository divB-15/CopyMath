# CopyMath

一个极简的 Windows 小工具：**左边写 LaTeX 公式，右边立刻看到渲染结果，一键复制成图片。**

排版完全交给本机的 LaTeX 引擎，所以输出质量和你在论文里看到的**一模一样**；程序本身约 2400 行 C++（19 个源文件，按模块分在 `src\` 下），不依赖任何第三方 GUI 库。

![CopyMath](assets/CopyMath.png)

---

> **当前版本：v1.3**（2026-11-05）—— 修好「设置 → 导言区」换行显示不出来的问题（编辑框现在能正常显示、回车换行、粘贴多行）；程序彻底绿色化，配置、临时文件、导出的图片全在程序文件夹内。各版本的详细改动见 [更新历史](#更新历史)。

---

## 目录

- [功能特性](#功能特性)
- [第一部分：如何使用](#第一部分如何使用)
  - [1. 先装 LaTeX（必须）](#1-先装-latex必须)
  - [2. 运行程序](#2-运行程序)
  - [3. 界面与快捷键](#3-界面与快捷键)
  - [4. 设置项说明](#4-设置项说明)
  - [5. 导言区：使用额外宏包与自定义符号](#5-导言区使用额外宏包与自定义符号)
  - [6. 使用日志：把写过的公式留一份底](#6-使用日志把写过的公式留一份底)
- [第二部分：如何从源码编译](#第二部分如何从源码编译)
  - [环境准备](#环境准备)
  - [方式一：双击 build.bat（推荐）](#方式一双击-buildbat推荐)
  - [方式二：手动执行命令](#方式二手动执行命令)
  - [构建系统做了什么](#构建系统做了什么)
  - [单元测试](#单元测试)
- [项目结构](#项目结构)
- [源码架构说明](docs/ARCHITECTURE.md)
- [工作原理](#工作原理)
- [常见问题](#常见问题)
- [更新历史](#更新历史)
- [关于 AI 使用](#关于-ai-使用)

---

## 功能特性

- **左侧源码，右侧预览**：所见即所得的 LaTeX 公式渲染
- **一键复制图片**：粘贴到 Word / 微信 / PPT / 画图都能直接用
- **一键保存 PNG**（`Ctrl+S`）：文件名自动带上时间戳，不会互相覆盖
- **图片自动贴合公式**：保存/复制出来的图四周只留一圈很窄的白边，不再拖着一条长空白
- **输出分辨率可调**（v1.2）：72～1200 DPI 随便填，默认 300；DPI 会写进 PNG 与剪贴板图像，粘进 Word 的物理尺寸才正常
- **总是置顶**（v1.2）：一个开关按钮，让窗口始终浮在最前面，方便对照着文档写公式；状态会记住
- **使用日志**（v1.2）：可选。每次复制/保存的公式连同时间、分辨率、文件名一起追加到 `logs.txt`，方便回溯
- **自定义导言区**：默认已引入 `mathtools / amssymb / physics / bm / mathrsfs / esint / tensor / yhmath`，还可以继续加
- **标准工程结构**：`CMakeLists.txt` + `src/`（按模块分目录）+ `tests/`（CTest）+ `docs/`，
  根目录只留工程文件（源码 / 构建脚本 / 文档），成品在 `bin\`，中间文件在 `build/`
- **纯 C++ + Win32 API + GDI+**：不使用 Qt / Electron 等任何第三方 GUI 框架
- **单一 exe 文件**：不需要安装，拷贝即用（绿色便携）
- **文件夹自包含**（v1.3）：配置 `CopyMath.cfg`、渲染临时文件 `tmp\`、导出的图片 `export\` 全在程序目录里，整个文件夹拷走即完整搬家，系统里不留垃圾
- **适配高 DPI 屏幕**：界面清晰不发虚

---

# 第一部分：如何使用

## 1. 先装 LaTeX（必须）

CopyMath **自己不做排版**，它调用本机的 LaTeX 引擎来出图。所以使用前需要安装一个 LaTeX 发行版：

| 系统 | 推荐发行版 | 说明 |
|---|---|---|
| Windows | [TeX Live](https://tug.org/texlive/) | 推荐，完整安装 |
| Windows | [MiKTeX](https://miktex.org/) | 更轻量，也可用 |

> **安装时请确保包含 `pdflatex` 与 `pdftocairo` 两个程序。**
> TeX Live 完整版自带；MiKTeX 首次使用时可能会提示自动补装宏包。
> 另外默认导言区里的 `physics / esint / tensor / yhmath` 也来自 TeX Live 完整版。

装好之后，程序**会自动探测**常见安装位置（`C:\texlive\2024\bin\windows` 等），
如果没探测到，打开程序的「设置」手动指定目录即可（见下文）。

## 2. 运行程序

直接双击 `bin\CopyMath.exe` 即可，无需安装。

`bin\` 里是「拷走就能用」的一整套（exe + 设置 + 运行时目录），程序会在 exe 旁边生成：

| 路径 | 内容 |
|---|---|
| `CopyMath.cfg` | 配置文件（保存你的设置） |
| `tmp\` | 渲染时的中间文件（`formula.tex/.pdf/.png/.log/.aux`），渲染完即删，退出时整个目录会被清掉 |
| `export\` | 默认的「导出图片路径」，保存的 PNG 落在这里 |

所以建议把整个 `bin\` 放在一个**有写入权限**的位置（比如 `<任意有写入权限的目录>\CopyMath\`，不要放在 `C:\Program Files` 下；
万一目录不可写，中间文件会自动退回系统临时目录，功能不受影响）。

> **从 v1.1 升级注意**：v1.1 的配置文件名是全小写的 `copymath.cfg`，v1.2 起统一为
> `CopyMath.cfg`。程序不会读取旧文件，第一次运行会重新建一份（TeX Live 路径会自动探测回来）；
> 旧配置文件可以直接删掉，也可以留着不管。

## 3. 界面与快捷键

```
┌────────────────────────┬────────────────────────┐
│                        │                        │
│   LaTeX 公式源码        │      渲染出来的公式      │
│   （在这里输入）         │      （在这里预览）      │
│                        │                        │
├────────────────────────┴────────────────────────┤
│ [渲染 (F5)] [重置 (F6)] [复制图片 (Ctrl+D)] [保存图片 (Ctrl+S)] [总是置顶] [设置] │
│ 状态提示信息…                                     │
└─────────────────────────────────────────────────┘
```

- 默认窗口大小是 **840 × 320**（逻辑像素），比 v1.1 的 1000 × 400 小了一圈，可以随便拉伸。
- 底部按钮的宽度按文字自适应；窗口被拖得很窄时，按钮会**自动换行**而不是被挤出窗口。
- 「总是置顶」是个开关：按下（呈凹陷状）就一直浮在最前面，再按一下取消。状态记在配置文件里。

| 快捷键 | 功能 |
|---|---|
| **F5** | **渲染**（把左侧公式渲染成右侧图片） |
| **F6** | **重置**（清空右侧预览 **和** 左侧输入框） |
| **Ctrl+D** 或 **F7** | **复制公式图片**到剪贴板 |
| **Ctrl+S** | **保存图片**到「导出图片路径」（文件名 `formula_时间戳.png`） |

**为什么复制图片不用 Ctrl+C？**
因为光标通常停在左侧输入框里，这时 Ctrl+C 会被输入框截走去复制**文字**。
所以复制图片改用 **Ctrl+D**（或 F7）——这两个键输入框不占用，无论焦点在哪都能触发。

保存图片用的是 **Ctrl+S**，同理与焦点无关；存完状态栏会报出文件名、像素尺寸和当前 DPI。

## 4. 设置项说明

点「设置」按钮，有六项：

| 设置项 | 说明 |
|---|---|
| **TeX Live 的 bin 目录** | 包含 `pdflatex.exe` 的文件夹，例如 `C:\texlive\2024\bin\windows`。程序会用这里的引擎渲染公式。填错时保存会提醒。 |
| **导出图片路径** | 点「保存图片」时 PNG 写到哪个文件夹（支持多级目录，不存在会自动创建）。**默认 `export`** —— 相对路径按程序所在目录展开，所以整个文件夹搬走仍指向自己旁边的 `export\`；也可以填绝对路径。文件名形如 `formula_20261105_132011.png`。 |
| **输出分辨率（DPI）** | **v1.2 新增。** 复制与保存共用，可填 `72`～`1200`，默认 `300`。调大更清晰、文件也更大。 |
| **自动记录日志** | **v1.2 新增。** 勾上后，每次「复制图片 / 保存图片」都往日志文件追加一条记录。 |
| **日志文件路径** | **v1.2 新增。** **默认为空**，需要自己指定（默认留空就不会写日志）。可以只填文件夹（程序会在里面用 `logs.txt`），也可以指定其它文件名。没勾日志开关时这一行是灰的。 |
| **导言区** | 多行文本框，**回车即可换行**，可直接从别处粘贴多行内容。见下一节。 |

设置会保存到程序目录的 `CopyMath.cfg`，下次启动自动读回。当然也可以直接修改 `CopyMath.cfg` 来完成设置。

> **关于 DPI 的两个细节**
> 1. DPI 决定 `pdftocairo` 的光栅化密度，也决定图片四周留白的像素数（300 DPI 下留 3 px，其它 DPI 等比缩放），所以换个 DPI 后出来的图仍然紧贴公式。
> 2. DPI 会写进 PNG 的 `pHYs` 元数据，也会写进剪贴板 `CF_DIB` 头的 `biXPelsPerMeter`。
>    这一步不是多余的：Word / PowerPoint 粘贴图片时会按这个字段换算物理尺寸，不填的话有些版本会按 96 DPI 处理，300 DPI 的公式粘进文档后会显得特别大。

## 5. 导言区：使用额外宏包与自定义符号

「导言区」就是 LaTeX 文档里 `\documentclass` 之后、`\begin{document}` 之前的那段内容。
你可以在里面写 `\usepackage{...}` 引入宏包，或用 `\newcommand` 定义自己的快捷键。

v1.2 的默认值是：

```latex
\usepackage{mathtools,amssymb,physics,bm,mathrsfs,esint,tensor,yhmath}
```

| 宏包 | 提供什么 |
|---|---|
| `mathtools` | `amsmath` 的增强版（会自动加载 `amsmath`） |
| `amssymb` | 额外数学符号（会自动加载 `amsfonts`） |
| `physics` | 物理公式常用命令：`\dv` `\pdv` `\bra` `\ket` `\qty` `\abs` … |
| `bm` | 粗体数学 `\bm{...}` |
| `mathrsfs` | 花体字母 `\mathscr` |
| `esint` | 更多积分号：`\oiint` `\oiiint` … |
| `tensor` | 张量指标 `\tensor` / `\indices` |
| `yhmath` | 大尺寸括号、根号等（Y&Y 数学扩展） |

举个进阶例子——再加几个自定义符号：

```latex
% 正体微分号 \d
\newcommand{\d}{\mathrm{d}}

% 省事的小写常量
\newcommand{\i}{\mathrm{i}}
\newcommand{\e}{\mathrm{e}}

% 常用算子
\DeclareMathOperator{\arsinh}{arsinh}
\DeclareMathOperator{\arcosh}{arcosh}
```

设好之后，左侧就可以直接写 `\d x`、`\arsinh x` 这类命令了。

> 提示：宏包名请写**准确**。如果名字拼错，渲染会失败，错误信息会以红字显示在右侧预览区。

> **关于这个多行框（v1.3 修复）**：v1.2 里它有两个毛病 —— 已有的换行显示不出来（整段挤成一行），而且按回车不会真的换行。原因是 Win32 的多行编辑框只把 **CRLF** 当换行（配置里存的是 LF），并且缺了 `ES_WANTRETURN`，回车被当成「点默认按钮」。v1.3 两头都补上了：进框转 CRLF、出框转回 LF，配置文件与生成的 `.tex` 里仍然是干净的 LF；从网页 / PDF 复制的文本若只带 `\n`，粘贴时也会自动补齐。

## 6. 使用日志：把写过的公式留一份底

在「设置」里勾上 **自动记录日志**，并填好日志文件路径（**默认为空，留空就不会写日志**；
可以只填一个文件夹，程序会在里面用 `logs.txt`），之后每次**复制图片**或**保存图片**，程序都会往这个文件**追加**一条记录：

```
=================================================================
[2026-09-28 13:20:11] 保存图片
  分辨率: 300 DPI
  像素  : 341 x 118
  文件  : <导出目录>\formula_20260928_132011.png
  源码  :
\frac{-b \pm \sqrt{b^2 - 4ac}}{2a}
```

- 文件是 **UTF-8**（新建时带 BOM），用记事本 / VS Code / Typora 打开都不会乱码；
- 记录是**追加**的，永远不会覆盖旧内容 —— 你甚至可以自己在里面写备注；
- 日志文件所在目录不存在会自动创建；
- 写失败不会打断操作，只在状态栏提醒一句「日志写入失败」，让你去检查路径。

> 提示：日志里存的是**左侧输入框里原始的那段源码**（含换行），所以格式、定界符都和你写的时候一模一样，可以直接复制回去重渲。

---

# 第二部分：如何从源码编译

如果你只想用程序，上面那部分就够了。以下是给想自己编译的人的说明。

工程是标准 C++ 布局：根目录放 `CMakeLists.txt` + `src/` + `tests/` + `assets/` + `docs/`，
编译中间文件全在 `build/`，成品输出到 `bin\`（exe 与 `CopyMath.cfg` 并排，拷走即用）。

## 环境准备

| 需要 | 用途 | 说明 |
|---|---|---|
| **MinGW-w64** | `g++.exe` 编译、`windres.exe` 嵌图标 | MSYS2 / w64devkit 自带；确认 MinGW-w64 安装目录的 `bin\` 下有 `g++.exe`、`windres.exe`、`mingw32-make.exe`（在 `PATH` 上即可） |
| **CMake ≥ 3.20** | 构建 | 装官方安装包即可；本机若没有，可在 `.build.local.bat` 设 `CMAKE` 指向任意一份，`build.bat` 会自动找 |
| TeX Live | **编译时不需要** | 只有运行程序才需要 `pdflatex.exe` 与 `pdftocairo.exe` |

## 方式一：双击 build.bat（推荐）

根目录的 `build.bat` 是个薄封装：找到 cmake → 配置（`MinGW Makefiles` + Release）→ 编译。
脚本内部 `cd /d "%~dp0"`，从哪儿双击都行。

```bat
[1/2] configure ...
[2/2] build ...
Build OK: bin\CopyMath.exe
```

看到 `Build OK` 就完成了，`bin\CopyMath.exe` 已经更新。

> `build.bat` 刻意只用 **ASCII + CRLF**：`.bat` 里写中文在 GBK 控制台下会乱码，
> 纯 LF 换行则会让 `^` 续行解析出错（这两条 `.editorconfig` 里也标了）。

## 方式二：手动执行命令

在项目根目录（`CMakeLists.txt` 那一层）执行：

```bat
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

> 装了 Ninja 的话也可以 `-G Ninja`；Git Bash 里同样的命令照用。

## 构建系统做了什么

| 配置 | 作用 |
|---|---|
| `project(CopyMath VERSION 1.3.0 LANGUAGES CXX RC)` | 同时启用 C++ 与 RC（资源）编译 |
| `add_executable(CopyMath WIN32 ...)` | `WIN32` = GUI 子系统（`-mwindows`），运行不弹黑框 |
| `target_include_directories(...)` | 每个模块目录一个 `-I`，源码里照旧写 `#include "config.h"` |
| `CMAKE_RC_FLAGS = -c 65001 -I<项目根>` | `app.rc` 是 UTF-8（版本信息有中文），且要能按相对路径找到 `assets/CopyMath.ico` |
| `-static-libgcc -static-libstdc++` | 静态链 GCC 运行库，分发的 exe 不依赖额外 DLL |
| `RUNTIME_OUTPUT_DIRECTORY = `bin\` | 成品落在 `bin\`，中间文件全在 `build/` |
| `enable_testing()` + `tests/` | CTest 跑 `test_util` |

## 单元测试

```bat
ctest --test-dir build --output-on-failure
:: 或者直接跑可执行文件看逐条结果
build\tests\test_util.exe
```

现在 **32 条断言**，集中在不依赖界面的纯函数上：`ToLf / ToCrlf`（v1.3 那个
「导言区显示成一行」的根因）、`DirNameOf`、`AbsFromExeDir`。
没有引第三方测试框架（一个 `CHECK` 宏 + 计数），以后想换 Catch2 / GoogleTest，
只改 `tests/` 即可，顶层 CMake 不用动。

---

## 项目结构

```text
CopyMath_v1.3/
├── CMakeLists.txt              构建入口（唯一的“真”构建配置）
├── build.bat                   Windows 一键编译：找 cmake → 配置 → 编译
├── README.md
├── LICENSE
├── .gitignore                  build/、*.o、*.exe、运行时生成物
├── .clang-format               代码风格（4 空格 / 100 列 / 大括号不换行）
├── .editorconfig               缩进与换行统一（.bat 强制 CRLF）
│
├── bin\                        ← 成品（整个目录拷走即用）
│   ├── CopyMath.exe            编译产物（双击运行）
│   ├── CopyMath.cfg            设置文件（读写在 exe 旁边）
│   ├── tmp\                     运行时生成：渲染中间文件（退出时清空）
│   └── export\                  运行时生成：默认导出目录
│
├── assets/                     构建输入
│   ├── CopyMath.ico            程序图标（嵌进 exe）
│   └── CopyMath.png            README 预览图
│
├── src/
│   ├── main.cpp                WinMain：DPI 感知、初始化、消息循环
│   ├── app.rc                  图标 + 版本信息（windres -c 65001）
│   ├── app/                    程序主体
│   │   ├── app.h               全局状态与跨模块接口的唯一声明处
│   │   ├── app_core.cpp        全局变量定义 + Px / SetStatus / ApplyTopmost
│   │   ├── actions.cpp         复制 / 保存 / 重置 / 探测 TeX Live / 日志封装
│   │   └── render_controller.cpp  渲染调度（版本号 + 忙标志 + 后台线程）
│   ├── ui/                     界面
│   │   ├── main_window.cpp     主窗口过程（建控件、布局、命令分发）
│   │   ├── settings_dialog.cpp 「设置」对话框
│   │   ├── preview_view.cpp    右侧预览子窗口（自绘）
│   │   └── edit_subclass.cpp   编辑框 Ctrl+A / 粘贴 CRLF 规范化
│   ├── render/                 渲染核心：拼 LaTeX → pdflatex → pdftocairo → GDI+
│   ├── config/                 读写 CopyMath.cfg（含默认导言区）
│   ├── log/                    使用日志
│   ├── clipboard/              位图写进剪贴板（CF_DIB + DPI）
│   └── util/                   编码 / 换行规范化 / 路径与目录工具
│
├── tests/
│   ├── CMakeLists.txt
│   └── test_util.cpp           32 条断言（CTest）
│
├── docs/
│   └── ARCHITECTURE.md         模块图、渲染数据流、线程模型、踩过的坑
│
└── build/                      **CMake 生成**：编译中间文件（不入库，可随时删）
```

| 位置 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层构建脚本：源文件清单、`-I` 路径、链接库、测试入口 |
| `build.bat` | 一键编译（等价于手动那三条命令） |
| `bin\` | 成品与运行时数据：exe、`CopyMath.cfg`、`tmp\`、`export\`；整个目录拷走即用 |
| `assets/` | 只放**构建输入**：图标、README 图；不放运行时要读的东西 |
| `src/app/` | 入口之外的“应用层”：全局状态、用户动作、渲染调度 |
| `src/ui/` | 所有窗口过程与控件细节 |
| `src/render/` `config/` `log/` `clipboard/` `util/` | 底层模块，不认识界面、不碰全局变量，可单独复用与测试 |
| `tests/` | 单元测试（目前是纯函数，链 `util.cpp` 即可） |
| `docs/ARCHITECTURE.md` | 改代码前先看这份：依赖方向、一次渲染的完整路径、六个坑 |
| `build/` | CMake 生成物，`.gitignore` 已忽略 |

依赖方向只有一条：**`ui` → `app` → `render / config / log / clipboard / util`**；
细节见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。

C++ 源码（19 个 `.cpp`/`.h`）约 2400 行，每个文件顶部都有注释说明职责。

## 工作原理

```
   左侧输入 LaTeX 源码（如 \frac{a}{b}）
              │
              │  ① 规范化：剥离自带的 $ / \[\] 定界符
              ▼
     拼接成一份完整的 .tex 文档（含用户导言区 + preview 宏包）
              │
              │  ② 调用 pdflatex                → formula.pdf
              │  ③ 调用 pdftocairo -r <DPI>     → formula.png
              ▼
     ④ 用 GDI+ 读入 PNG → 按墨迹二次裁剪 → 显示在右侧
              │
              ▼
     ⑤ 复制时以 CF_DIB 写入剪贴板（带 DPI）/ 保存时编码为 PNG 落盘
              │
              ▼
     ⑥ 若开了日志，把「时间 + 操作 + DPI + 尺寸 + 源码」追加到 logs.txt
```

细节说明：

- **紧致裁切（两步走）**：
  ① 生成的文档用 `preview` 宏包的 `tightpage` 让 PDF 页面贴紧公式；公式按内联
  `$\displaystyle ...$` 排版，而**不是** v1.0 的 `\[ ... \]`——后者生成的盒子宽度是
  「整整一行」（`\hsize`），公式在其中居中，于是图的两侧会留下一大片与公式无关的空白
  （这正是 v1.0 的问题：公式只有 335px 宽，出图却有 1624px 宽）。
  ② 出图后扫描非白像素的包围盒做二次裁剪，四周各留一圈窄白边（300 DPI 下 3px，按 DPI 等比缩放）。
  两步合起来，单行公式、含 `\\` 的多行公式、`align` 这类对齐环境，出来的图都贴合公式本身。
- **分辨率**：由设置的 DPI 决定（`pdftocairo -r`），默认 300；出图后还会把该 DPI
  写回 GDI+ 位图对象（`SetResolution`），这样保存的 PNG 自带正确的 `pHYs`。
- **临时文件**（v1.3 改了位置）：全部放在程序目录的 `tmp\` 下（v1.2 是 `%TEMP%\CopyMath\`），
  渲染前会先清一遍（含上次残留的 `.aux`），程序退出时再清一次并把空目录删掉；
  只有程序目录不可写时才退回 `%TEMP%\CopyMath\`。
- **不卡界面**：渲染在后台线程进行，结果通过自定义 Windows 消息回传主线程。
- **错误可见**：LaTeX 的报错会以红字直接显示在右侧预览区，而不是只藏在状态栏。

## 常见问题

**Q：右侧一直显示「找不到 pdflatex.exe」？**
说明没找到 LaTeX 引擎。请确认已安装 TeX Live/MiKTeX，然后在「设置」里把
**包含 `pdflatex.exe` 的那个文件夹**填进「TeX Live 的 bin 目录」。
例如 TeX Live 2024 默认是 `C:\texlive\2024\bin\windows`。

**Q：渲染报「Undefined control sequence」？**
说明用到了没引入的命令。把对应的 `\usepackage{...}` 写进「设置 → 导言区」。

**Q：渲染报 `File 'physics.sty' not found` 之类？**
默认导言区用到的宏包都随 **TeX Live 完整版**安装。如果你装的是精简版（或 MiKTeX 没自动补包），
要么补装这些宏包，要么把「导言区」里用不到的名字删掉。

**Q：粘到 Word 里的公式特别大 / 特别小？**
调「设置 → 输出分辨率（DPI）」。图像像素数随 DPI 变大，但物理尺寸（英寸/厘米）保持不变，
Word 是按 DPI 字段换算的。想让它更大就调大 DPI（得到更清晰的图、尺寸不变）；
想改变物理尺寸，请在 Word 里直接缩放图片。

**Q：日志文件是空的 / 没生成？**
按顺序查三件事：①「设置 → 自动记录日志」勾上了吗；②「日志文件路径」填了吗（**默认为空，
留空就不会写**）；③日志只在**复制或保存成功后**才写。如果状态栏出现「日志写入失败」，
多半是路径所在目录没有写权限，换一个位置即可。

**Q：渲染速度有点慢？**
每次渲染都要启动 `pdflatex` 进程，通常需要 1～3 秒，这是外部调用 LaTeX 的固有开销。
程序采用**手动渲染**（按 F5）正是为了避免边打字边渲染造成的等待。

**Q：能渲染中文吗？**
当前使用的是 `pdflatex`，对中文支持有限。公式内嵌的少量英文/数学符号没有问题。

**Q：换一台电脑要重装吗？**
不用。把 `bin\` 整个目录拷过去就能跑（`CopyMath.cfg` 与导出目录都在里面，用的是相对路径）；
源码那一套不用带。但**目标机器同样需要安装 LaTeX**，换了机器也别忘了在「设置」里确认 TeX Live 路径。

**Q：改了导言区，为什么预览没变化？**
设置是即时生效的：点「保存」后对话框关闭，程序会用它重新渲染一次右侧预览。
若预览确实没变，先看右侧有没有红字报错（比如宏包名拼错）。

---

## 更新历史

### v1.3 — 2026-11-05

**修复**

- **「设置 → 导言区」显示不出换行**（v1.2 的问题，本次重点）。
  现象：配置文件里导言区明明一行一条，进了设置对话框却挤成一整行（换行处变成方块），
  按回车也换不了行；而生成的 `.tex` 源文件里换行又是对的。两个原因叠加：
  1. Win32 的多行 `EDIT` 控件只把 **CRLF** 当换行，而配置 / 内存里用的是 **LF**，
     直接 `SetWindowTextW` 塞进去，控件就把它当成一行；
  2. 那个编辑框漏了 `ES_WANTRETURN`，在非对话框窗口里回车被当成「按下默认按钮」，
     根本不插入换行。
  改法：新增 `ToCrlf()` / `ToLf()` 两个规范化函数 —— **进编辑框前转 CRLF，取回后转回 LF**
  （配置文件和生成的 `.tex` 仍是干净的 LF）；编辑框补上 `ES_WANTRETURN`；
  多行框粘贴时若剪贴板里是「裸 `\n`」（从网页 / PDF 复制常见）也自动补成 CRLF。
- **拼 `.tex` 前保证导言区以换行结尾**：否则导言区最后一行若是 `%` 注释，
  会把后面拼上去的 `\usepackage[active,tightpage]{preview}` 一起注释掉，直接渲染失败。
- **`build.bat` 重写为 ASCII + CRLF**：原来那份是 UTF-8 + LF，在 cmd 下中文乱码，
  且 `^` 续行解析出错（双击编译会报一堆「不是内部或外部命令」）。

**改进（便携化）**

- **渲染中间文件挪进程序目录**：`%TEMP%\CopyMath\` → **`<程序目录>\tmp\`**，
  渲染完即删，程序退出时再清一次并删除空目录；目录不可写时才退回系统临时目录。
- **默认导出目录改成 `<程序目录>\export`**（配置里存相对路径 `export`），
  配置中的路径现在也支持相对写法，一律按 exe 所在目录展开 —— 整个文件夹搬走仍然有效。
- 设置里「导出图片路径」为空时自动补成 `export`，不再要求用户先去设置里填。

**重构（目录结构）**

- **源码全部收进 `src\`，按模块分目录**：根目录只留工程文件（`CMakeLists.txt` / `build.bat` /
  README / LICENSE / 风格配置），成品统一收进 `bin\`；源码在 `src\app`（入口 + 动作 +
  渲染调度）、`src\ui`（主窗口 / 设置 / 预览 / 编辑框）、`src\render`、`src\config`、`src\log`、
  `src\clipboard`、`src\util` 里。
- 原来 1400 多行的 `main.cpp` 拆成 **9 个文件**：`app.h`（全局状态与接口的唯一声明）、
  `app_core.cpp`（全局变量定义 + 小工具）、`main.cpp`（只留 WinMain）、`actions.cpp`、
  `render_controller.cpp`、`main_window.cpp`、`settings_dialog.cpp`、`preview_view.cpp`、
  `edit_subclass.cpp`。依赖方向固定为 **ui → app → 底层模块**。
- 编译脚本跟着源码走（当时在 `src\build.bat`，v1.3 收尾时换成了根目录的 CMake + `build.bat`），
  用 `-I` 给每个模块目录加头文件搜索路径，源码里的 `#include "config.h"` 之类一行没变。

**工程化（对齐标准 C++ 布局）**

- 构建系统换成 **CMake**：`CMakeLists.txt` 是唯一构建入口，`build.bat` 退化成几行封装；
  链接选项、`-I` 路径、RC 编码（`-c 65001`）都写进 CMake，不再靠一长串手敲命令。
- 新增 **`tests/`**（CTest + 32 条断言，覆盖换行规范化与路径工具）与 **`docs/ARCHITECTURE.md`**
  （模块图、渲染数据流、线程模型、踩过的坑）。
- 补齐 **`.editorconfig`** / **`.clang-format`** / 标准 `.gitignore`；
  `main.cpp` 从 `src/app/` 移到 `src/` 根（入口文件单独可见），图标与预览图统一进 `assets/`。
- **成品收进 `bin\`**：`RUNTIME_OUTPUT_DIRECTORY` 指向 `bin`，根目录不再直接躺 exe；
  `CopyMath.cfg`、`tmp\`、`export\` 都跟着 exe 待在 `bin\` 里，整个 `bin\` 拷走即可使用。

**其它**

- 版本号 → **1.3.0.0**（`app.rc` 的 `FILEVERSION` / `FileVersion` 与窗口标题同步）。
- 源码补注释：说明 CRLF 这个坑与 `tmp\` / `export\` 的由来。

### v1.2 — 2026-09-28

**新增**

- **「总是置顶」开关按钮**：主界面底部多一个切换按钮（`BS_AUTOCHECKBOX | BS_PUSHLIKE`，
  按下时呈凹陷状），让窗口始终浮在最前面，方便对照 Word / PPT 里的正文写公式。
  状态记进配置文件，下次启动自动恢复。
- **输出分辨率（DPI）可调**：原来写死 300，现在在「设置」里可填 `72`～`1200`（默认 300），
  复制与保存共用。改动点：
  1. `render.cpp` 的 `pdftocairo -r` 用该值，并按 DPI 等比缩放墨迹留白（300 DPI→3px，600 DPI→6px）；
  2. `clipboard.cpp` 把它写进 `BITMAPINFOHEADER.biXPelsPerMeter / biYPelsPerMeter`
     —— Word 粘贴图片时按这个字段换算物理尺寸，写 0 的话有些版本会按 96 DPI 处理；
  3. 保存前 `Bitmap::SetResolution()`，让 GDI+ 的 PNG 编码器写出正确的 `pHYs` 元数据。
- **可选的使用日志**：设置里勾选「自动记录日志」并填好路径后，每次复制/保存都会往
  `logs.txt` **追加**一条记录（时间 / 操作 / DPI / 像素 / 文件名 / 原始源码）。
  日志路径**默认为空**（留空即不写）；只填文件夹时程序会在里面用 `logs.txt`。
  文件为 UTF-8（新建时带 BOM）、用 CRLF 换行，记事本直接打开可读；目录不存在会自动创建。
  新增 `log.cpp` / `log.h` 两个文件承担这部分逻辑。
- **递归创建目录**：导出目录和日志目录都支持多级路径（原来是单层 `CreateDirectoryW`）。
- 设置对话框保存时会**顺手校验** TeX Live 路径和 DPI 取值，填错会提示而不静默失败。

**改进**

- **默认导言区**换成 `mathtools,amssymb,physics,bm,mathrsfs,esint,tensor,yhmath`
  （原来只有 `amsmath,amssymb,amsfonts,mathtools` + `xcolor,bm`），开箱即可写物理/张量/多重积分公式。
- **默认窗口尺寸**由 1000 × 400 收小到 **840 × 320**；同时让底部按钮行支持**自动换行**，
  窗口拖窄也不会把按钮挤出可视区。
- 设置对话框改为**固定尺寸** + `AdjustWindowRectEx` 换算外框，修掉 v1.1 里最右侧控件被边框裁掉一点的问题；
  日志开关未勾选时，路径输入框与「浏览」按钮置灰。
- 状态栏在渲染成功/复制/保存时都会报出 **DPI**，方便确认当前设置是否生效。

**修正**

- **程序名统一为 `CopyMath`**：`CopyMath.exe` / `CopyMath.ico` / `CopyMath.cfg`，
  版本信息的 `InternalName` 与 `OriginalFilename` 也跟着改（原来全小写 `copymath`）。
  临时目录相应改成 `%TEMP%\CopyMath\`。
  ⚠ 配置文件名变了，**v1.1 的 `copymath.cfg` 不会被读取**，首次运行会重建一份（TeX Live 路径自动探测）。
- `LoadConfig` 对导言区改为「**整体替换**」而不是「追加」：v1.2 会在读取前预置一份默认导言区，
  若仍用 `+=` 拼接，默认宏包会和文件里的宏包叠在一起（同一个宏包加载两遍）。

### v1.1 — 2026-09-26

**新增**

- **`Ctrl+S` 保存图片**：以前只能用鼠标点按钮。走加速键表，光标停在输入框里也照样触发。
- **exe 版本信息**：右键 exe → 属性 → 详细信息可以看到「CopyMath - LaTeX 公式渲染工具 1.1.0.0」；
  窗口标题也带上了版本号。

**修复**

- **保存/复制出来的图片两侧有一大片白边**（v1.0 的问题）。
  根因：`\[ ... \]` 生成的行间公式，排版盒子宽度等于**整整一行**（`\hsize`），公式在盒子中间居中；
  而 `preview` 的 `tightpage` 是按盒子裁页面的，于是裁出来的是「整行宽」的图。
  实测同一个公式：盒子给出 **1624 × 122 px**，公式墨迹其实只有 **335 × 112 px**，两侧各空约 645 px。

  改法（都在 `render.cpp`）：

  1. 公式改用内联 `$\displaystyle ...$` 排版 —— 盒子宽度 = 公式自身的自然宽度。
     含 `\\` 的多行内容改用 `gathered` 包裹（行内数学里 `\\` 非法），用户导言区没引 `amsmath` 时自动补上；
  2. 加 `\setlength\PreviewBorder{2pt}` —— 不留保护白边时页面边框正好压在墨迹上，最外一圈笔画会被削掉；
  3. 出图后按像素扫描非白像素的包围盒，再裁一次，四周留 3 px
     —— `align` / `equation` 这类无法内联的对齐环境靠它裁齐。

  之后同一个公式出图 **341 × 118 px**，四周恰好各 3 px。
- **顺带修掉一个隐患**：用 `\[ \]` 时，宽度超过一行的长公式会被 PDF 页面裁掉；改用内联数学后不再受这个限制。

**文档**

- 更正 v1.0 README 里「用了 `preview` 的 `tightpage`，所以图片没有多余白边」的说法 —— 当时只有高度方向成立。

### v1.0 — 2026-09-24

首个正式发布版本（开源上线）。

- 左侧写 LaTeX 源码、右侧预览的 Win32 界面，手动渲染（`F5`），后台线程不卡界面；
- 一键复制公式图片到剪贴板（`Ctrl+D` / `F7`）：以 `CF_DIB` 写入，Word / 微信 / 画图都能直接粘贴；
- 一键保存 PNG，文件名带时间戳（`formula_20260924_083012.png`）；
- 可自定义导言区，支持 `physics`、`esint`、`tensor` 等额外宏包与自定义命令；
- 设置持久化到 exe 同目录的 `copymath.cfg`（UTF-8），绿色便携、拷贝即用；
- 纯 Win32 API + GDI+，单一 exe，无第三方依赖（但运行时需要本机安装 TeX Live）；
- 补全全部源码注释、重写 README、生成 MIT `LICENSE` 与 `.gitignore`。

## 关于 AI 使用

> 本项目由 AI 在人类作者实质性指导下生成，按 MIT 许可证授权。
> 
> AI 生成的代码可能存在人类未察觉的疏漏，请自行评估 AI 生成代码的质量与安全性，本项目不提供任何形式的担保。
