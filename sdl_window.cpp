#include "sdl_window.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace {

constexpr float rotation_step = 5.0f;
constexpr float move_step = 0.1f;
constexpr float pi = 3.14159265358979323846f;
constexpr float mouse_sensitivity = 0.0025f; // Radians per pixel of relative motion.
constexpr float pitch_limit = 89.0f * pi / 180.0f;
constexpr const char* window_title =
    "Software Rasterizer | Mouse: look | Q/E: rotate | WASD/Space/Ctrl: move | Esc: quit";

[[noreturn]] void throw_sdl_error(const char* operation)
{
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

Uint8 encode_channel(float value)
{
    return static_cast<Uint8>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

} // namespace

SDLWindow::SDLWindow(int width, int height) : width_(width), height_(height)
{
    if (width_ <= 0 || height_ <= 0) {
        throw std::invalid_argument("Window width and height must be positive integers.");
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        throw_sdl_error("Cannot initialize SDL video");
    }
    video_initialized_ = true;

    try {
        // Omitting SDL_WINDOW_RESIZABLE keeps the framebuffer size fixed.
        window_ = SDL_CreateWindow(window_title, width_, height_, SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (window_ == nullptr) {
            throw_sdl_error("Cannot create the SDL window");
        }
        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (renderer_ == nullptr) {
            throw_sdl_error("Cannot create the SDL renderer");
        }
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STREAMING, width_, height_);
        if (texture_ == nullptr) {
            throw_sdl_error("Cannot create the framebuffer texture");
        }
        if (!SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_NONE) ||
            !SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST) ||
            !SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255)) {
            throw_sdl_error("Cannot configure framebuffer presentation");
        }
        // Keep reporting motion at window edges while the window has focus.
        if (!SDL_SetWindowRelativeMouseMode(window_, true)) {
            throw_sdl_error("Cannot enable relative mouse mode");
        }
    } catch (...) {
        release();
        throw;
    }
}

SDLWindow::~SDLWindow()
{
    release();
}

void SDLWindow::present(const std::vector<Eigen::Vector3f>& frame, float angle)
{
    if (frame.size() != static_cast<std::size_t>(width_) * height_) {
        throw std::invalid_argument("Framebuffer size does not match the window.");
    }

    void* pixels = nullptr;
    int pitch = 0;
    if (!SDL_LockTexture(texture_, nullptr, &pixels, &pitch)) {
        throw_sdl_error("Cannot lock the framebuffer texture");
    }
    for (int y = 0; y < height_; ++y) {
        // Both buffers store rows from top to bottom. SDL may pad each row.
        auto* row = static_cast<Uint8*>(pixels) + static_cast<std::size_t>(y) * pitch;
        for (int x = 0; x < width_; ++x) {
            const auto& color = frame[static_cast<std::size_t>(y) * width_ + x];
            // RGBA32 defines byte order consistently on all supported platforms.
            row[0] = encode_channel(color.x());
            row[1] = encode_channel(color.y());
            row[2] = encode_channel(color.z());
            row[3] = 255;
            row += 4;
        }
    }
    SDL_UnlockTexture(texture_);

    if (!SDL_RenderClear(renderer_) ||
        !SDL_RenderTexture(renderer_, texture_, nullptr, nullptr) ||
        !SDL_RenderPresent(renderer_)) {
        throw_sdl_error("Cannot present the framebuffer");
    }
    const std::string title = std::string(window_title) + " | angle: " +
                              std::to_string(static_cast<int>(angle)) + " deg";
    if (!SDL_SetWindowTitle(window_, title.c_str())) {
        throw_sdl_error("Cannot update the window title");
    }
}

bool SDLWindow::process_events(float& angle, Eigen::Vector3f& camera_position,
                               float& camera_pitch, float& camera_yaw)
{
    const SDL_WindowID window_id = SDL_GetWindowID(window_);
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
             event.window.windowID == window_id)) {
            return false;
        }
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            if (event.motion.windowID == window_id) {
                camera_yaw = std::remainder(
                    camera_yaw + event.motion.xrel * mouse_sensitivity, 2.0f * pi);
                // SDL's Y axis points down; positive pitch looks up.
                camera_pitch = std::clamp(
                    camera_pitch - event.motion.yrel * mouse_sensitivity,
                    -pitch_limit, pitch_limit);
            }
            continue;
        }
        if (event.type != SDL_EVENT_KEY_DOWN || event.key.windowID != window_id) {
            continue;
        }
        switch (event.key.key) {
        case SDLK_ESCAPE:
            return false;
        case SDLK_Q:
            angle = std::remainder(angle + rotation_step, 360.0f);
            break;
        case SDLK_E:
            angle = std::remainder(angle - rotation_step, 360.0f);
            break;
        case SDLK_W:
            camera_position.z() -= move_step;
            break;
        case SDLK_S:
            camera_position.z() += move_step;
            break;
        case SDLK_A:
            camera_position.x() -= move_step;
            break;
        case SDLK_D:
            camera_position.x() += move_step;
            break;
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            camera_position.y() -= move_step;
            break;
        case SDLK_SPACE:
            camera_position.y() += move_step;
            break;
        default:
            break;
        }
    }
    return true;
}

void SDLWindow::release() noexcept
{
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (video_initialized_) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        video_initialized_ = false;
    }
}
