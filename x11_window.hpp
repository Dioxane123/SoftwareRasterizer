#ifndef X11_WINDOW_HPP
#define X11_WINDOW_HPP

#include <eigen3/Eigen/Core>
#include <X11/Xlib.h>

#include <vector>

// X11 only presents the CPU framebuffer and receives keyboard/window events.
class X11Window {
public:
    X11Window(int width, int height);
    ~X11Window();
    X11Window(const X11Window&) = delete;
    X11Window& operator=(const X11Window&) = delete;

    void present(const std::vector<Eigen::Vector3f>& frame, float angle);

    // Drain pending events without blocking the continuous render loop.
    bool process_events(float& angle);

private:
    // X11 visuals describe where each RGB channel belongs in a native pixel.
    struct PixelChannel {
        unsigned long maximum = 0;
        unsigned int shift = 0;

        explicit PixelChannel(unsigned long mask = 0);
        unsigned long encode(float value) const;
    };

    void release() noexcept;

    int width_, height_;
    Display* display_ = nullptr;
    Window window_ = 0;
    GC gc_ = nullptr;
    XImage* image_ = nullptr;
    Atom wm_protocols_ = 0;
    Atom wm_delete_window_ = 0;
    PixelChannel red_, green_, blue_;
};

#endif // X11_WINDOW_HPP
