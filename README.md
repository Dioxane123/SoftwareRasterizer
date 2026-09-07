# Software Rasterizer

一个学习games101之后的学习项目。

当前入口会打开一个 800 × 600 的窗口，在黑色背景上绘制白色三角形线框。
窗口使用 Linux 的 X11 系统接口；Wayland 桌面可通过 XWayland 运行。
不使用 OpenCV、SDL、Qt 或 OpenGL，像素由项目自己的 CPU 光栅器生成。

## 构建与运行

需要 C++17 编译器、CMake、项目原有的 Eigen3，以及系统的 X11 开发文件。
Debian/Ubuntu 对应的软件包为 `g++`、`cmake`、`libeigen3-dev`、`libx11-dev`。

```sh
cmake -S . -B build
cmake --build build -j
./build/software_rasterizer
```

请在图形桌面的终端中运行，环境中需要有可用的 `DISPLAY`。
窗口获得键盘焦点后：

- **Q**：绕模型 Z 轴逆时针旋转 5°。
- **E**：绕模型 Z 轴顺时针旋转 5°。
- **Esc** 或窗口关闭按钮：退出。

按住 Q/E 可以通过键盘自动重复连续旋转。窗口尺寸固定，标题栏显示当前角度。

主循环持续渲染，终端每约一秒打印一次 `FPS: 12.34`。
FPS 按本统计周期完成绘制并提交给窗口的帧数除以实际经过的秒数计算，
包含光栅化、像素格式转换和图像提交的时间；它不是显示器的刷新率。

## 变换矩阵

`main.cpp` 中保留了三个独立的矩阵构造函数，方便学习和调整：

- `get_model_matrix(angle_degrees)`：构造绕 Z 轴的旋转矩阵。
- `get_view_matrix(eye_position)`：相机朝向固定为 -Z、上方为 +Y，按相机位置构造平移矩阵。
- `get_projection_matrix(fov, aspect, near, far)`：构造右手系透视投影矩阵，深度范围映射到 NDC 的 [-1, 1]。

入口分别调用 `set_model()`、`set_view()` 和 `set_projection()` 传入矩阵；
每次旋转先清除上一帧，再以 `Primitive::Line` 绘制三条边。
示例三角形在整个旋转过程中均位于视野内；完整视锥裁剪、深度测试和颜色插值仍是后续学习内容。
