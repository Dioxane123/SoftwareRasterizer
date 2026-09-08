#include "rasterizer.hpp"
#include "x11_window.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>

namespace {

constexpr int window_width = 800;
constexpr int window_height = 600;
constexpr float pi = 3.14159265358979323846f;

// Model: rotate around the model's Z axis. Positive angles turn counterclockwise.
Eigen::Matrix4f get_model_matrix(float angle_degrees)
{
    const float radians = angle_degrees * pi / 180.0f;
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    Eigen::Matrix4f model = Eigen::Matrix4f::Identity();
    model(0, 0) = c;
    model(0, 1) = -s;
    model(1, 0) = s;
    model(1, 1) = c;
    return model;
}

// View: the camera looks along -Z with +Y pointing up, without camera rotation.
Eigen::Matrix4f get_view_matrix(const Eigen::Vector3f& eye_position)
{
    Eigen::Matrix4f view = Eigen::Matrix4f::Identity();
    view.block<3, 1>(0, 3) = -eye_position;
    return view;
}

// Perspective projection: near/far are positive distances; NDC depth is [-1, 1].
Eigen::Matrix4f get_projection_matrix(float vertical_fov_degrees,
                                      float aspect_ratio,
                                      float near_plane,
                                      float far_plane)
{
    const float half_fov = vertical_fov_degrees * pi / 360.0f;
    const float focal_length = 1.0f / std::tan(half_fov);

    Eigen::Matrix4f projection = Eigen::Matrix4f::Zero();
    projection(0, 0) = focal_length / aspect_ratio;
    projection(1, 1) = focal_length;
    projection(2, 2) = -(far_plane + near_plane) / (far_plane - near_plane);
    projection(2, 3) = -2.0f * far_plane * near_plane / (far_plane - near_plane);
    projection(3, 2) = -1.0f;
    return projection;
}

} // namespace

int main()
{
    try {
        rst::rasterizer rasterizer(window_width, window_height);
        // The camera is at z = 5: the first triangle is nearer (z = 0.5),
        // and the second is farther (z = -1). They overlap and stay in view as they rotate.
        const auto positions = rasterizer.load_positions({
            {-0.4f, 1.2f, -0.5f}, {-1.4f, -0.8f, -0.5f}, {0.6f, -0.8f, -0.5f},
            {0.5f, 1.3f, 1.0f}, {-0.5f, -0.9f, 1.0f}, {1.6f, -0.9f, 1.0f}});
        // Draw the near triangle first so the far triangle must pass the depth test.
        const auto indices = rasterizer.load_indices({{0, 1, 2}, {3, 4, 5}});
        // Colors correspond one-to-one with the six positions: RGB, then yellow/cyan/magenta.
        const auto colors = rasterizer.load_colors({
            {153.0f, 153.0f, 255.0f}, {153.0f, 153.0f, 255.0f}, {153.0f, 153.0f, 255.0f},
            {255.0f, 255.0f, 0.0f}, {0.0f, 255.0f, 255.0f}, {255.0f, 0.0f, 255.0f}});

        // All three matrices enter the rasterizer through its public setters.
        rasterizer.set_view(get_view_matrix({0.0f, 0.0f, 5.0f}));
        rasterizer.set_projection(get_projection_matrix(
            45.0f, static_cast<float>(window_width) / window_height, 0.1f, 50.0f));

        X11Window window(window_width, window_height);
        float angle = 0.0f;
        using Clock = std::chrono::steady_clock;
        auto last_fps_report = Clock::now();
        std::size_t frame_count = 0;
        std::cout << "Q: rotate counterclockwise; E: rotate clockwise; Esc: quit.\n";
        while (window.process_events(angle)) {
            rasterizer.clear(rst::Buffers::Color | rst::Buffers::Depth);
            rasterizer.set_model(get_model_matrix(angle));
            rasterizer.draw(positions, colors, indices, rst::Primitive::Triangle);
            window.present(rasterizer.frame_buffer(), angle);
            ++frame_count;

            const auto now = Clock::now();
            const double elapsed = std::chrono::duration<double>(now - last_fps_report).count();
            if (elapsed >= 1.0) {
                const double fps = static_cast<double>(frame_count) / elapsed;
                std::cout << "FPS: " << std::fixed << std::setprecision(2) << fps << std::endl;
                frame_count = 0;
                last_fps_report = now;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "Software Rasterizer: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
