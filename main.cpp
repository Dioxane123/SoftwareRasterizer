#include "rasterizer.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr int window_width = 800;
constexpr int window_height = 600;
constexpr float pi = 3.14159265358979323846f;
constexpr float rotation_step = 5.0f;

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

// X11 visuals describe where each RGB channel belongs in a native pixel.
struct PixelChannel {
    unsigned long maximum = 0;
    unsigned int shift = 0;

    explicit PixelChannel(unsigned long mask = 0)
    {
        if (mask == 0) {
            return;
        }
        while ((mask & 1UL) == 0) {
            mask >>= 1;
            ++shift;
        }
        maximum = mask;
    }

    unsigned long encode(float value) const
    {
        const float scaled = std::clamp(value, 0.0f, 1.0f) *
                             static_cast<float>(maximum);
        return static_cast<unsigned long>(scaled + 0.5f) << shift;
    }
};

// X11 only presents the CPU framebuffer and receives keyboard/window events.
class X11Window {
public:
    X11Window(int width, int height) : width_(width), height_(height)
    {
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr) {
            throw std::runtime_error(
                "Cannot open the X11 display. Run from a graphical desktop "
                "with DISPLAY set (X11 or Wayland with XWayland).");
        }

        try {
            const int screen = DefaultScreen(display_);
            Visual* visual = DefaultVisual(display_, screen);
            if (visual->c_class != TrueColor) {
                throw std::runtime_error("This demo requires an X11 TrueColor visual.");
            }

            window_ = XCreateSimpleWindow(
                display_, RootWindow(display_, screen), 0, 0, width_, height_,
                0, BlackPixel(display_, screen), BlackPixel(display_, screen));
            XSelectInput(display_, window_, ExposureMask | KeyPressMask |
                                           StructureNotifyMask);

            // Keep the window and framebuffer sizes identical for this demo.
            XSizeHints size_hints{};
            size_hints.flags = PMinSize | PMaxSize;
            size_hints.min_width = size_hints.max_width = width_;
            size_hints.min_height = size_hints.max_height = height_;
            XSetWMNormalHints(display_, window_, &size_hints);

            wm_protocols_ = XInternAtom(display_, "WM_PROTOCOLS", False);
            wm_delete_window_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
            XSetWMProtocols(display_, window_, &wm_delete_window_, 1);
            const unsigned long process_id = static_cast<unsigned long>(getpid());
            XChangeProperty(display_, window_, XInternAtom(display_, "_NET_WM_PID", False),
                            XA_CARDINAL, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(&process_id), 1);

            gc_ = XCreateGC(display_, window_, 0, nullptr);
            image_ = XCreateImage(display_, visual, DefaultDepth(display_, screen),
                                  ZPixmap, 0, nullptr, width_, height_, 32, 0);
            if (gc_ == nullptr || image_ == nullptr) {
                throw std::runtime_error("Cannot create X11 drawing resources.");
            }

            // XDestroyImage frees this data, so it must be allocated with malloc/calloc.
            image_->data = static_cast<char*>(std::calloc(
                static_cast<std::size_t>(height_),
                static_cast<std::size_t>(image_->bytes_per_line)));
            if (image_->data == nullptr) {
                throw std::runtime_error("Cannot allocate the window image.");
            }
            red_ = PixelChannel(image_->red_mask);
            green_ = PixelChannel(image_->green_mask);
            blue_ = PixelChannel(image_->blue_mask);

            XStoreName(display_, window_, "Software Rasterizer | Q/E: rotate | Esc: quit");
            XMapWindow(display_, window_);
        } catch (...) {
            release();
            throw;
        }
    }

    ~X11Window() { release(); }
    X11Window(const X11Window&) = delete;
    X11Window& operator=(const X11Window&) = delete;

    void present(const std::vector<Eigen::Vector3f>& frame, float angle)
    {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                // frame_buffer() already stores rows from top to bottom.
                const auto index = static_cast<std::size_t>(y) * width_ + x;
                const auto& color = frame[index];
                const unsigned long pixel = red_.encode(color.x()) |
                                            green_.encode(color.y()) |
                                            blue_.encode(color.z());
                XPutPixel(image_, x, y, pixel);
            }
        }
        XPutImage(display_, window_, gc_, image_, 0, 0, 0, 0, width_, height_);
        const std::string title = "Software Rasterizer | Q/E: rotate | Esc: quit | angle: " +
                                  std::to_string(static_cast<int>(angle)) + " deg";
        XStoreName(display_, window_, title.c_str());
        XFlush(display_);
    }

    // Block while idle. Return true when the image needs to be drawn again.
    bool wait_for_redraw(float& angle)
    {
        for (;;) {
            XEvent event{};
            XNextEvent(display_, &event);
            if (event.type == Expose && event.xexpose.count == 0) {
                return true;
            }
            if (event.type == KeyPress) {
                const KeySym key = XLookupKeysym(&event.xkey, 0);
                if (key == XK_Escape) {
                    return false;
                }
                if (key == XK_q || key == XK_Q) {
                    angle = std::remainder(angle + rotation_step, 360.0f);
                    return true;
                }
                if (key == XK_e || key == XK_E) {
                    angle = std::remainder(angle - rotation_step, 360.0f);
                    return true;
                }
            }
            if (event.type == ClientMessage && event.xclient.message_type == wm_protocols_ &&
                event.xclient.format == 32 &&
                static_cast<Atom>(event.xclient.data.l[0]) == wm_delete_window_) {
                return false;
            }
            if (event.type == DestroyNotify && event.xdestroywindow.window == window_) {
                window_ = 0;
                return false;
            }
        }
    }

private:
    void release() noexcept
    {
        if (image_ != nullptr) {
            XDestroyImage(image_);
            image_ = nullptr;
        }
        if (display_ != nullptr) {
            if (gc_ != nullptr) {
                XFreeGC(display_, gc_);
                gc_ = nullptr;
            }
            if (window_ != 0) {
                XDestroyWindow(display_, window_);
                window_ = 0;
            }
            XCloseDisplay(display_);
            display_ = nullptr;
        }
    }

    int width_, height_;
    Display* display_ = nullptr;
    Window window_ = 0;
    GC gc_ = nullptr;
    XImage* image_ = nullptr;
    Atom wm_protocols_ = 0;
    Atom wm_delete_window_ = 0;
    PixelChannel red_, green_, blue_;
};

} // namespace

int main()
{
    try {
        rst::rasterizer rasterizer(window_width, window_height);
        // An equilateral triangle centered at the model origin, fully in view as it rotates.
        const auto positions = rasterizer.load_positions({
            {0.0f, 1.2f, 0.0f}, {-1.0f, -0.6f, 0.0f}, {1.0f, -0.6f, 0.0f}});
        const auto indices = rasterizer.load_indices({{0, 1, 2}});
        const auto colors = rasterizer.load_colors({
            {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}});

        // All three matrices enter the rasterizer through its public setters.
        rasterizer.set_view(get_view_matrix({0.0f, 0.0f, 5.0f}));
        rasterizer.set_projection(get_projection_matrix(
            45.0f, static_cast<float>(window_width) / window_height, 0.1f, 50.0f));

        X11Window window(window_width, window_height);
        float angle = 0.0f;
        std::cout << "Q: rotate counterclockwise; E: rotate clockwise; Esc: quit.\n";
        do {
            rasterizer.clear(rst::Buffers::Color | rst::Buffers::Depth);
            rasterizer.set_model(get_model_matrix(angle));
            rasterizer.draw(positions, colors, indices, rst::Primitive::Triangle);
            window.present(rasterizer.frame_buffer(), angle);
        } while (window.wait_for_redraw(angle));
    } catch (const std::exception& error) {
        std::cerr << "Software Rasterizer: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
