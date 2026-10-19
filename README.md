# SpaceViewer

跨平台磁盘占用查看器，交互方式和 SpaceSniffer 同一类：矩形树图按文件和文件夹的大小占面积，扫描过程中图形会增长，点击文件夹进入，可以按名称过滤、查看详情、打开文件并导出 CSV。

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

在 Windows 7 SP1 上用能在该系统运行的 MinGW-w64（GCC 9–13，MSVCRT，不要用只带 UCRT 且自身无法在 Windows 7 启动的新包）：

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

引擎在 `src/`。窗口代码按系统分开：`src/platform/win`、`src/platform/linux`、`src/platform/apple`。

## Ubuntu

需要 X11 和 Wayland 两套依赖。会话里有 `WAYLAND_DISPLAY` 时用 Wayland，连不上再退回 X11。也可以用 `SPACEVIEWER_BACKEND=wayland` 或 `SPACEVIEWER_BACKEND=x11` 指定。

```sh
sudo apt install build-essential cmake pkg-config \
  libx11-dev libxft-dev \
  libwayland-dev wayland-protocols libxkbcommon-dev \
  libfontconfig1-dev libfreetype6-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/spaceviewer
```

没有 CMake 时可以 `sh build-linux.sh`。选择文件夹需要 `zenity` 或 `kdialog`；没有这两个程序时，在路径栏输入路径后回车。X11 高分屏请让 `Xft.dpi` 等于实际 DPI。Wayland 使用输出的整数缩放，界面按物理像素绘制。

## macOS

需要 Xcode 命令行工具，系统至少 macOS 10.15。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/SpaceViewer
```

或 `sh build-macos.sh`。Retina 屏使用窗口的 `backingScaleFactor`，位图按物理像素绘制。

## 许可

Copyright (C) 2026 Wenbo Zhong

本程序是自由软件，按 GNU General Public License v3.0 发布。你可以再发布它和/或按该协议第三版的条款修改它。完整条款见仓库根目录的 `LICENSE`。程序按现状提供，不附带任何担保。
