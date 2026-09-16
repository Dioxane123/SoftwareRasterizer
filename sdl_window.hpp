#ifndef SDL_WINDOW_HPP
#define SDL_WINDOW_HPP

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

// Presents the CPU framebuffer and receives keyboard/mouse/window events through SDL3.
// Construct, use, and destroy this window on the main thread.
class SDLWindow {
public:
    SDLWindow(int width, int height);
    ~SDLWindow();
    SDLWindow(const SDLWindow&) = delete;
    SDLWindow& operator=(const SDLWindow&) = delete;

    void present(const std::vector<Eigen::Vector3f>& frame, float angle);

    // Drain pending events; camera pitch/yaw are accumulated in radians.
    bool process_events(float& angle, Eigen::Vector3f& camera_position,
                        float& camera_pitch, float& camera_yaw);

private:
    void release() noexcept;

    int width_, height_;
    bool video_initialized_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
};

#endif // SDL_WINDOW_HPP
