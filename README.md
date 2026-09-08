# Software Rasterizer

一个学习games101之后的学习项目。

当前入口会打开一个 800 × 600 的窗口，在黑色背景上绘制两个重叠的彩色三角形。
窗口和键盘事件使用 SDL3，面向 Linux、Windows 和 macOS。
变换、光栅化、深度测试和颜色插值由项目自己的 CPU 光栅器完成；
SDL3 负责选择系统显示后端，并将 CPU 帧缓冲作为纹理显示到窗口。

## 构建与运行

需要支持 C++17 的 C/C++ 编译工具链、CMake 3.16+、Eigen 3.3+ 和 SDL 3.2+。
CMake 优先使用已安装的 SDL3；找不到时，自动下载 SDL 3.4.16 并默认编译为静态库。
首次下载需要网络，源码和构建产物保存在构建目录的 `_deps/` 下。

### Linux

Debian/Ubuntu 可以先安装编译工具、Eigen 和 SDL 源码构建所需的 X11 后端开发文件：

```sh
sudo apt install build-essential cmake pkg-config libeigen3-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev
```

如果发行版提供 `libsdl3-dev`，也可以直接安装并使用系统 SDL3。
需要原生 Wayland 或其他 SDL 后端时，开发依赖见 [SDL3 Linux 构建说明](https://wiki.libsdl.org/SDL3/README-linux)。

在项目根目录构建 Release 版本：

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
./build-release/software_rasterizer
```

请在图形桌面中运行。SDL 会根据可用的后端连接 X11 或 Wayland 显示服务。

### macOS

安装 Xcode Command Line Tools 后，使用 Homebrew 安装依赖：

```sh
brew install cmake eigen sdl3
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
./build-release/software_rasterizer
```

SDL3 软件包信息见 [Homebrew sdl3](https://formulae.brew.sh/formula/sdl3)。

### Windows

安装 Visual Studio 的“使用 C++ 的桌面开发”工作负载、CMake 和 [vcpkg](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started)。
以下 PowerShell 示例使用 Visual Studio 2022，并假设 vcpkg 位于 `C:/vcpkg`：

```powershell
C:/vcpkg/vcpkg.exe install eigen3:x64-windows sdl3:x64-windows
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build-windows --config Release --parallel
./build-windows/Release/software_rasterizer.exe
```

Visual Studio 使用 `--config Release` 选择构建配置。
使用共享版 SDL3 时，CMake 会把 `SDL3.dll` 复制到可执行文件旁边。
依赖查找通过 [vcpkg 的 CMake 工具链](https://learn.microsoft.com/en-us/vcpkg/users/buildsystems/cmake-integration) 完成。

### 自定义依赖与 Debug 构建

如果依赖安装在自定义位置，可在配置时传入 `-DCMAKE_PREFIX_PATH="/path/to/install"`，
或分别设置指向包配置目录的 `Eigen3_DIR` 和 `SDL3_DIR`。
使用 `-DSOFTWARE_RASTERIZER_FETCH_SDL3=OFF` 可以关闭自动下载；此时必须提供已安装的 SDL3。
离线使用 SDL 源码时，可设置 `-DFETCHCONTENT_SOURCE_DIR_SDL3=/path/to/SDL`。
SDL3 的集成方式见 [官方 CMake 文档](https://wiki.libsdl.org/SDL3/README-cmake)。

Linux/macOS 的 Makefiles 或 Ninja 构建可使用 `-DCMAKE_BUILD_TYPE=Debug` 切换到调试配置；
Visual Studio 等多配置生成器则使用 `cmake --build build-windows --config Debug`。

## 操作

窗口获得键盘焦点后：

- **Q**：绕模型 Z 轴逆时针旋转 5°。
- **E**：绕模型 Z 轴顺时针旋转 5°。
- **W / S**：相机沿 -Z / +Z 移动。
- **A / D**：相机沿 -X / +X 移动。
- **空格 / 左右 Ctrl**：相机沿 +Y / -Y 移动。
- **Esc** 或窗口关闭按钮：退出。

相机每次按键移动 0.1 个世界单位。按住按键可通过键盘自动重复连续操作。
窗口尺寸固定，标题栏显示当前角度；高 DPI 显示器上使用最近邻缩放呈现帧缓冲。

主循环持续渲染，终端每约一秒打印一次 `FPS: 12.34`。
FPS 按本统计周期完成绘制并提交给窗口的帧数除以实际经过的秒数计算，
包含光栅化、像素格式转换和图像提交的时间；它不是显示器的刷新率。

## 代码组织

- `main.cpp`：场景数据、相机位置、矩阵生成和渲染循环。
- `sdl_window.hpp` / `sdl_window.cpp`：SDL3 生命周期、窗口、事件和帧缓冲显示。
- `rasterizer.hpp` / `rasterizer.cpp`：CPU 光栅化、深度缓冲和颜色插值。
- `triangle.hpp` / `triangle.cpp`：三角形顶点和属性。

`SDL_main.h` 仅在 `main.cpp` 中包含，按 [SDL3 程序入口约定](https://wiki.libsdl.org/SDL3/README-main-functions)
处理 Windows 等平台的入口差异。

## 变换矩阵

`main.cpp` 中保留了三个独立的矩阵构造函数，方便学习和调整：

- `get_model_matrix(angle_degrees)`：构造绕 Z 轴的旋转矩阵。
- `get_view_matrix(eye_position)`：相机朝向固定为 -Z、上方为 +Y，按相机位置构造平移矩阵。
- `get_projection_matrix(fov, aspect, near, far)`：构造右手系透视投影矩阵，深度范围映射到 NDC 的 [-1, 1]。

入口分别调用 `set_model()`、`set_view()` 和 `set_projection()` 传入矩阵；
每帧清除颜色与深度缓冲，再以 `Primitive::Triangle` 绘制填充三角形。
相机朝向保持不变，移动后由相机位置重新生成 view 矩阵。

当前光栅器尚未实现完整视锥裁剪，三角形包围盒也未限制在屏幕范围内。
相机移动使三角形离开屏幕或穿过相机平面时，仍可能触发缓冲区越界；这是当前光栅器的已知限制。
