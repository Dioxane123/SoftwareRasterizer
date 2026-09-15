# Software Rasterizer

一个学习games101之后的学习项目。

当前入口会打开一个 800 × 600 的窗口，在黑色背景上绘制从 `assets/cube.obj` 加载的灰色立方体。
初始相机朝向原点，可以同时看到立方体的三个面。
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

## OBJ 模型加载

默认模型 `assets/cube.obj` 是手工创建的边长为 2、中心位于原点的立方体。
文件只包含 8 个顶点和 6 个四边形面；加载器自动生成 12 个三角形，并为 8 个顶点补齐默认灰色。
默认模型路径由 CMake 指向源码中的文件，因此从其他工作目录启动程序也能加载它。
也可以通过第一个命令行参数指定模型：

```sh
./build-release/software_rasterizer /path/to/model.obj
```

模型位置、大小和坐标轴按文件原样保留。显示其他模型时，可按需要调整模型矩阵和相机。
如果把可执行文件移到没有源码的环境中，需要同时携带 OBJ 文件，并通过命令行指定其路径。

`obj_loader.hpp` / `obj_loader.cpp` 只依赖 Eigen 和 C++ 标准库，与窗口和光栅器分离。
`mesh::load_obj()` 从文件读取，`mesh::parse_obj()` 从输入流读取，均返回 `mesh::MeshData`：

| 成员 | 类型 | 约定 |
| --- | --- | --- |
| `positions` | `std::vector<Eigen::Vector3f>` | 顶点位置 |
| `indices` | `std::vector<Eigen::Vector3i>` | 每项为一个三角形，索引从 0 开始 |
| `colors` | `std::vector<Eigen::Vector3f>` | 与位置数组等长，RGB 范围为 0～255 |

可以直接接入现有接口：

```cpp
const auto model = mesh::load_obj("assets/cube.obj", Eigen::Vector3f(180, 180, 180));
const auto positions = rasterizer.load_positions(model.positions);
const auto indices = rasterizer.load_indices(model.indices);
const auto colors = rasterizer.load_colors(model.colors);
```

当前支持范围：

- `v x y z`，以及带可选权重的 `v x y z w`。多边形位置使用 `xyz`，忽略自由曲面权重 `w`。
- 顶点颜色扩展 `v x y z r g b` 和 `v x y z w r g b`，文件中的 RGB 使用 0～1，加载后转换为 0～255。
  缺失颜色默认使用 `(180, 180, 180)`，可以通过函数的第二个参数指定其他默认颜色。
- 面顶点支持 `v`、`v/vt`、`v//vn`、`v/vt/vn`，仅取位置索引。
  正索引从 1 开始，负索引相对于该面之前已经定义的顶点；索引必须引用已经定义的顶点。
- 使用耳切法拆分简单平面多边形，支持凸多边形和凹多边形，并保留原有绕序。
  自相交、退化或明显不共面的多边形会报错。
- 支持注释、空行、CRLF、UTF-8 BOM 和反斜杠续行。
- UV、文件法线、材质和分组等记录暂不进入输出数组；当前光照继续使用光栅器计算的面法线。
  无法读取文件或遇到非法几何数据时抛出异常，解析错误会包含对应行号。

### 验证加载器

默认构建包含测试程序，验证默认颜色、颜色转换、索引格式、凹多边形三角化、非法输入，
以及 cube 的面朝向、封闭性和通过现有光栅器渲染的结果。测试无需打开窗口：

```sh
ctest --test-dir build-release --output-on-failure
```

Visual Studio 等多配置构建需添加 `-C Release`。可通过 `-DBUILD_TESTING=OFF` 关闭测试构建。

## 操作

窗口获得键盘焦点后：

- **移动鼠标**：左右转头、上下抬头；向右移动向右看，向上移动向上看。
- **Q**：绕模型 Z 轴逆时针旋转 5°。
- **E**：绕模型 Z 轴顺时针旋转 5°。
- **W / S**：相机沿 -Z / +Z 移动。
- **A / D**：相机沿 -X / +X 移动。
- **空格 / 左右 Ctrl**：相机沿 +Y / -Y 移动。
- **Esc** 或窗口关闭按钮：退出。

相机每次按键移动 0.1 个世界单位。按住按键可通过键盘自动重复连续操作。
鼠标使用相对模式：窗口获得焦点时隐藏并捕获鼠标，移动到窗口边缘后仍可连续转向。
灵敏度为每像素 0.0025 弧度，可在 `sdl_window.cpp` 的 `mouse_sensitivity` 中调整；俯仰角限制为 ±89°。
窗口尺寸固定，标题栏显示当前角度；高 DPI 显示器上使用最近邻缩放呈现帧缓冲。

主循环持续渲染，终端每约一秒打印一次 `FPS: 12.34`。
FPS 按本统计周期完成绘制并提交给窗口的帧数除以实际经过的秒数计算，
包含光栅化、像素格式转换和图像提交的时间；它不是显示器的刷新率。

## 代码组织

- `main.cpp`：场景数据、相机位置、矩阵生成和渲染循环。
- `obj_loader.hpp` / `obj_loader.cpp`：OBJ 解析、默认颜色和多边形三角化。
- `assets/cube.obj`：用于默认场景的立方体模型。
- `tests/obj_loader_tests.cpp`：模型加载和无窗口渲染测试。
- `sdl_window.hpp` / `sdl_window.cpp`：SDL3 生命周期、窗口、事件和帧缓冲显示。
- `rasterizer.hpp` / `rasterizer.cpp`：CPU 光栅化、深度缓冲和颜色插值。
- `triangle.hpp` / `triangle.cpp`：三角形顶点和属性。

`SDL_main.h` 仅在 `main.cpp` 中包含，按 [SDL3 程序入口约定](https://wiki.libsdl.org/SDL3/README-main-functions)
处理 Windows 等平台的入口差异。

## 变换矩阵

`main.cpp` 中保留了三个独立的矩阵构造函数，方便学习和调整：

- `get_model_matrix(angle_degrees)`：构造绕 Z 轴的旋转矩阵。
- `get_view_matrix(eye_position, pitch, yaw)`：按相机位置和以弧度表示的俯仰、水平转角构造 view 矩阵，零角度时朝向 -Z。
- `get_projection_matrix(fov, aspect, near, far)`：构造右手系透视投影矩阵，深度范围映射到 NDC 的 [-1, 1]。

入口分别调用 `set_model()`、`set_view()` 和 `set_projection()` 传入矩阵；
每帧清除颜色与深度缓冲，再以 `Primitive::Triangle` 绘制填充三角形。
每帧使用键盘更新后的位置和鼠标更新后的角度重新生成 view 矩阵。

当前光栅器在透视除法前进行六面视锥裁剪，并将三角形包围盒限制在屏幕范围内。
填充渲染会剔除屏幕空间顺时针的面，模型应使用从外部观察为逆时针的顶点绕序。
