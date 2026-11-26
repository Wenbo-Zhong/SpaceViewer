# SpaceViewer

跨平台磁盘占用查看器，交互方式和 SpaceSniffer 同一类：矩形树图按文件和文件夹的大小占面积，扫描过程中图形会增长，点击文件夹进入，可以按名称过滤、查看详情、打开文件并导出 CSV。

界面默认使用随程序打包的 Inter（拉丁文字）和思源黑体（简体或繁体中文）。字体及许可证位于 `assets/fonts/`；Windows 将字体和许可证嵌入 EXE，macOS 放入 .app，Linux 分发包包含程序与字体。

界面语言会按系统语言自动选择简体中文、繁体中文或英文；其他系统语言默认显示英文。文案位于 [`locales/`](locales/README.md)，新增语言可通过提交 `locales/<BCP-47>/strings.json` 参与翻译。

Windows 发行版是单个 exe，不带额外 DLL。程序只调用 Windows 7 就有的接口，并声明 DPI 感知，在高分屏上按物理像素绘制。

## 功能

- 选择文件夹、磁盘，或把文件夹拖进窗口（Windows、macOS）。命令行也可以传入路径。
- 矩形树图总览最多显示三层；单一子目录保持为完整的大块，避免堆叠路径标题。单击文件夹立即在原区域展开，内容和文字直接呈现，不做缩放、擦除或渐入动画；只保留当前展开路径，切换分支时收回之前手动展开的分支，保留周围目录和面积比例；双击或右键“进入目录”才放大到整个视图。工具栏和 `Backspace`、`Alt+Up` 返回，`Alt+Left` / `Alt+Right` 后退和前进。
- 扫描磁盘根目录时，尚未统计的区域会随扫描缩小。可用空间不占矩形图。目录标题和内部方块之间有分割线。大小变化用大约四分之一秒的过渡；进入目录时从点击区域连续放大，返回时反向缩回，约 0.16 秒完成，文字渐入并保留周围方块的退场过程；本机有 OpenCL 时，方块填充走 OpenCL。
- 先确定当前区域中能容纳紧凑名称的项目，再按这些项目的相对大小重新铺满整个父区域；放不下文字的尾部项目不单独占位，不留下空白条带。图形面积是当前可读项目间的相对比例，文件大小、目录总计、状态栏和导出仍使用实际数据。放大或进入目录后可以显示更多项目。目录标题靠左，以暖色底和细边框区分父子容器。
- 扫描结束或中途停止后继续听系统的文件变更（Windows 为 `ReadDirectoryChangesW`，Linux 为 inotify，macOS 为 FSEvents）。新增、删除和体积变化会实时更新矩形图；变化项目或最近的可见父目录会短暂提亮，约 0.4 秒后恢复。鼠标悬停先提亮后回落，停留时持续保持明显但较弱的高亮，不再描边。变更挤爆通知缓冲时，状态栏会提示按 `F5` 重扫。
- 包含、排除（`*.jpg;*.tmp`）和最小大小（`1M`、`1.5G`）。
- 悬停和选中时在底部显示大小、占比和完整路径。双击文件打开。右键可以打开、定位、复制路径或进入文件夹。
- 导出当前视图的 CSV（UTF-8，带 BOM，Excel 可直接打开）。
- `F5` 重新扫描当前文件夹，扫描中可以停止。

大小是文件的逻辑大小，不是“占用簇大小”。符号链接和重解析点会计入一次并跳过，避免循环。

## 启动、菜单和视图控制

无启动路径时自动显示“选择您的空间”：双击磁盘直接扫描，也可以输入目录后按开始，或用“路径”浏览文件夹。取消只关闭选择窗口，保留灰色工作区。菜单“文件”及工具栏“打开”可随时重新选择。

菜单提供新窗口、导出、停止、关闭视图、当前视图刷新和选中子树刷新；窗口菜单可后退、前进、返回扫描根目录、调节展开细节、按文件类型着色、显示可用/未知空间，也可以平铺或层叠多个 SpaceViewer 窗口。配置保存在当前用户的 AppData/SpaceViewer/settings.ini。

文件菜单可保存和打开 SpaceViewer `.svs` 扫描快照，启动窗口也有快照入口。快照保留完整扫描树和逻辑大小，离线查看时不监听磁盘；不兼容原版 `.sns` 格式。

快捷键：`Ctrl+O` 选择路径，`Ctrl+N` 新窗口，`Ctrl+Home` 扫描根目录，`Ctrl+-` / `Ctrl++` 调节细节，`Ctrl+G` 文件类型着色，`Ctrl+F` 可用空间，`Ctrl+U` 未知空间。

## Windows 7 上编译

在 Windows 7 SP1 上使用兼容该系统的 CMake，以及能在该系统运行的 MinGW-w64（GCC 9–13，MSVCRT，不要用只带 UCRT 且自身无法在 Windows 7 启动的新包）：

```bat
build-win7.bat
```

生成当前目录下的 `SpaceViewer.exe`。`-static` 会把运行库打进 exe，目标机器不需要额外安装 MinGW DLL。

自检（不打开窗口）：

```bat
SpaceViewer.exe --self-test
```

用 Visual Studio 2017 或 2019 时，工具集需要能在 Windows 7 上运行（VS 2017）。VS 2022 的编译器本身不能装在 Windows 7 上，但可以在更高版本的 Windows 上加上下面的 CMake 选项，编出能在 Windows 7 SP1（已安装通用 C 运行库 KB2999226）运行的程序。

```bat
cmake -S . -B build
cmake --build build --config Release
```

CMake 使用 `/MT`，因此 Release 目录里的 `SpaceViewer.exe` 不依赖 VC 运行库 DLL。清单文件同时写了 Windows 7 能识别的 `dpiAware` 和 Windows 10 及更新系统使用的 `PerMonitorV2`。

## 源码布局

共享代码按职责划分，平台目录处理系统接口与原生窗口：

| 目录 | 职责 |
| --- | --- |
| `src/app/` | 应用状态、命令分发、键鼠事件和文本编辑；`app.hpp`、`host.hpp`、`input.hpp` 是宿主接口 |
| `src/model/` | 紧凑扫描树、文件类别、过滤和可见大小计算 |
| `src/scan/` | 扫描生命周期、快照发布、任务池、批量入树和节点更新 |
| `src/view/` | 矩形树图布局、绘制、控件、视觉参数、动效和 OpenCL 填充；输出 `Frame` 给原生窗口 |
| `src/io/` | CSV 和扫描快照读写 |
| `src/i18n/` | 文案、格式化和 Unicode 辅助函数；生成的文案头文件不手动修改 |
| `src/platform/posix/` | macOS/Linux 共用的目录枚举和卷信息 |
| `src/platform/{win,apple,linux}/` | 原生窗口、各系统文件监听及平台专有扫描规则 |
| `tests/` | 现有自检和共享逻辑、命令一致性、扫描回归测试 |

`App::State` 和 `Scanner::Impl` 的内部头文件只供实现使用。增加菜单或快捷键时，通用动作在 `app/commands.cpp` 实现，入口只做事件映射；绘制与输入命中仍共享上一帧的区域信息。目录枚举通过共享扫描层批量入树与累计大小，各系统保留原有大小写、符号链接和跨卷规则。

所有构建脚本使用同一份 CMake 源文件列表，避免新增文件后遗漏某个平台。

## 开发验证

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`selftest` 验证过滤、布局、导出、快照和子树刷新；`refactor` 验证共享辅助函数、键盘/工具栏/原生命令的一致性及扫描变更。应用的 `--self-test` 入口保持兼容。多配置生成器运行测试时加 `-C Release`。

只验证共享逻辑、不构建原生窗口时，可配置 `-DSPACEVIEWER_BUILD_APP=OFF`。Linux 此模式不需要 X11/Wayland 开发包；macOS 仍使用系统框架验证 FSEvents 和卷信息。

## Ubuntu

需要 X11 和 Wayland 两套依赖。会话里有 `WAYLAND_DISPLAY` 时默认用 Wayland，连不上再退回 X11。也可以用 `--x11` 或 `--wayland` 参数强制选择，例如 `./SpaceViewer --x11`；环境变量 `SPACEVIEWER_BACKEND=wayland` 或 `SPACEVIEWER_BACKEND=x11` 也可指定后端。

```sh
sudo apt install build-essential cmake pkg-config \
  libx11-dev libxft-dev \
  libwayland-dev wayland-protocols libxkbcommon-dev \
  libfontconfig1-dev libfreetype6-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/spaceviewer
```

也可以 `sh build-linux.sh`。选择文件夹需要 `zenity` 或 `kdialog`；没有这两个程序时，在路径栏输入路径后回车。X11 高分屏请让 `Xft.dpi` 等于实际 DPI。Wayland 使用输出的整数缩放，界面按物理像素绘制。

生成包含程序和字体的分发包（安装上述编译依赖后）：

```sh
sh package-linux.sh
```

输出为 `out/SpaceViewer-ubuntu-x86_64.tar.gz`（ARM64 上为 `aarch64`）。解压后运行包内的 `spaceviewer`，并保留包内的 `fonts/`。程序使用系统提供的 X11、Wayland、Fontconfig、FreeType 和 C++ 运行库。

GitHub Actions 在推送、拉取请求或手动触发时构建三平台产物。可从对应运行记录的 Artifacts 下载 Windows x64 单文件 EXE、macOS 通用 .app 压缩包及 Ubuntu x64 tar.gz。CMake 构建需要 3.24 或更新版本。

## macOS

需要 Xcode 命令行工具，系统至少 macOS 10.15。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
open build/SpaceViewer.app
```

或 `sh build-macos.sh`，生成 `build/SpaceViewer.app`。首次启动可选扫描整个启动磁盘，也可选其他磁盘或文件夹。扫描器最多用 8 个线程并发读取目录。Windows 磁盘扫描显示按容量估算的百分比；macOS 扫描期间显示活动进度条和文件读取速度，完成后进度条填满，不显示估算百分比。访问桌面、文稿、下载目录或外接磁盘时，macOS 会按需显示授权提示。若扫描时遇到系统保护的目录，应用会提示到“系统设置 > 隐私与安全性 > 完全磁盘访问”手动授权，再重新扫描。macOS 不允许应用自行开启完全磁盘访问。Retina 屏使用窗口的 `backingScaleFactor`，位图按物理像素绘制。

## 许可

Copyright (C) 2026 Wenbo Zhong

本程序是自由软件，按 GNU General Public License v3.0 发布。你可以再发布它和/或按该协议第三版的条款修改它。完整条款见仓库根目录的 `LICENSE`。程序按现状提供，不附带任何担保。
