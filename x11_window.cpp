#include "x11_window.hpp"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {

constexpr float rotation_step = 5.0f;

} // namespace

X11Window::PixelChannel::PixelChannel(unsigned long mask)
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

unsigned long X11Window::PixelChannel::encode(float value) const
{
    const float scaled = std::clamp(value, 0.0f, 1.0f) *
                         static_cast<float>(maximum);
    return static_cast<unsigned long>(scaled + 0.5f) << shift;
}

X11Window::X11Window(int width, int height) : width_(width), height_(height)
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

X11Window::~X11Window()
{
    release();
}

void X11Window::present(const std::vector<Eigen::Vector3f>& frame, float angle)
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

bool X11Window::process_events(float& angle)
{
    while (XPending(display_) > 0) {
        XEvent event{};
        XNextEvent(display_, &event);
        if (event.type == KeyPress) {
            const KeySym key = XLookupKeysym(&event.xkey, 0);
            if (key == XK_Escape) {
                return false;
            }
            if (key == XK_q || key == XK_Q) {
                angle = std::remainder(angle + rotation_step, 360.0f);
            }
            if (key == XK_e || key == XK_E) {
                angle = std::remainder(angle - rotation_step, 360.0f);
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
    return true;
}

void X11Window::release() noexcept
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
