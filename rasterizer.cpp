#include "rasterizer.hpp"
#include <algorithm>
#include <cmath>
#include <eigen3/Eigen/Eigen>

using namespace Eigen;

rst::pos_buf_id rst::rasterizer::load_positions(const std::vector<Eigen::Vector3f>& positions)
{
    auto id = get_next_id();
    pos_buf.emplace(id, positions);

    return {id};
}

rst::ind_buf_id rst::rasterizer::load_indices(const std::vector<Eigen::Vector3i>& indices)
{
    auto id = get_next_id();
    ind_buf.emplace(id, indices);

    return {id};
}

void rst::rasterizer::set_pixel(const Eigen::Vector3f& point,
                                const Eigen::Vector3f& color)
{
    // Screen-space coordinates use the bottom-left as the origin, while the
    // frame buffer is stored row by row from top to bottom.
    if (!(point.x() >= 0.0f && point.x() < static_cast<float>(width) &&
          point.y() >= 0.0f && point.y() < static_cast<float>(height)))
    {
        return;
    }

    const int x = static_cast<int>(point.x());
    const int y = static_cast<int>(point.y());
    const auto index = static_cast<std::size_t>(height - 1 - y) *
                           static_cast<std::size_t>(width) +
                       static_cast<std::size_t>(x);

    if (index < frame_buf.size())
    {
        frame_buf[index] = color;
    }
}

void rst::rasterizer::draw_line(const Eigen::Vector3f& begin, const Eigen::Vector3f& end)
{
    const Eigen::Vector3f line_color = {255, 255, 255};
    auto x1 = begin.x();
    auto y1 = begin.y();
    auto x2 = end.x();
    auto y2 = end.y();
    
    auto dx = std::abs(x2 - x1);
    auto dy = std::abs(y2 - y1);
    auto sx = (x1 < x2) ? 1 : -1;
    auto sy = (y1 < y2) ? 1 : -1;

    int err = dx - dy;
    while(true){
        rst::rasterizer::set_pixel(Eigen::Vector3f(x1, y1, 1.0), line_color);
        if (x1 == x2 && y1 == y2) {
            break;
        }
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }

    return;
}

bool insideTriangle(float x, float y, const Vector3f* _v){
    std::array<Eigen::Vector3f, 3> v={(Eigen::Vector3f(_v[0].x(), _v[0].y(), 1.0f),
                                      Eigen::Vector3f(_v[1].x(), _v[1].y(), 1.0f),
                                      Eigen::Vector3f(_v[2].x(), _v[2].y(), 1.0f))};
    Eigen::Vector3f p(x, y, 1.0f);
    bool allPositive = true;
    bool allNegative = true;
    for(int i = 0; i < 3; ++i){
        Eigen::Vector3f edge = v[(i + 1) % 3] - v[i];
        Eigen::Vector3f toPoint = p - v[i];
        if(edge.cross(toPoint).z() < 0){
            allPositive = false;
        }else{
            allNegative = false;
        }
    }

    return allPositive || allNegative;
}

void rst::rasterizer::rasterize_triangle(const Triangle& t){
    std::array<Eigen::Vector4f, 3> v = {
        (projection * view * model * t.toVector4f()[0]),
        (projection * view * model * t.toVector4f()[1]),
        (projection * view * model * t.toVector4f()[2])
    };
    std::transform(std::begin(v), std::end(v), std::begin(v), [](auto& vec){
        return Eigen::Vector4f(vec.x()/vec.w(), vec.y()/vec.w(), vec.z()/vec.w(), 1.0f);
    });
    int x_min = std::floor(std::min({v[0].x(), v[1].x(), v[2].x()}));
    int x_max = std::ceil(std::max({v[0].x(), v[1].x(), v[2].x()}));
    int y_min = std::floor(std::min({v[0].y(), v[1].y(), v[2].y()}));
    int y_max = std::ceil(std::max({v[0].y(), v[1].y(), v[2].y()}));

    for(int i = x_min; i <= x_max; ++i){
        for(int j = y_min; j <= y_max; ++j){
            if(insideTriangle(i + 0.5f, j + 0.5f, t.v)){
                rst::rasterizer::set_pixel(Eigen::Vector3f(i, j, 1.0), t.color[0]);
            }
        }
    }

}

void rst::rasterizer::set_model(const Eigen::Matrix4f& m){
    model = m;
}

void rst::rasterizer::set_view(const Eigen::Matrix4f& v){
    view = v;
}

void rst::rasterizer::set_projection(const Eigen::Matrix4f& p){
    projection = p;
}

