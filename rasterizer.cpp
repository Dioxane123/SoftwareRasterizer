#include "rasterizer.hpp"
#include <algorithm>
#include <cmath>
#include <Eigen/Eigen>
#include <limits>
#include <stdexcept>
#include <tuple>

using namespace Eigen;

rst::pos_buf_id rst::rasterizer::load_positions(const std::vector<Eigen::Vector3f>& positions)
{
    auto id = get_next_id();
    pos_buf.emplace(id, positions);

    return {id};
}

rst::col_buf_id rst::rasterizer::load_colors(const std::vector<Eigen::Vector3f>& colors)
{
    auto id = get_next_id();
    col_buf.emplace(id, colors);

    return {id};
}

rst::ind_buf_id rst::rasterizer::load_indices(const std::vector<Eigen::Vector3i>& indices)
{
    auto id = get_next_id();
    ind_buf.emplace(id, indices);

    return {id};
}

int rst::rasterizer::get_index(int x, int y){
    return (height - 1 - y) * width + x;
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
    const auto index = static_cast<size_t>(get_index(x, y));

    if (index < frame_buf.size())
    {
        frame_buf[index] = color;
    }
}

void rst::rasterizer::draw_line(const Eigen::Vector3f& begin, const Eigen::Vector3f& end)
{
    const Eigen::Vector3f line_color = {1, 1, 1};

    // Bresenham advances by whole pixels and must be able to reach its endpoint.
    int x1 = static_cast<int>(std::floor(begin.x()));
    int y1 = static_cast<int>(std::floor(begin.y()));
    const int x2 = static_cast<int>(std::floor(end.x()));
    const int y2 = static_cast<int>(std::floor(end.y()));
    
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

static bool insideTriangle(float x, float y, const Eigen::Vector3f(&_v)[3]){
    std::array<Eigen::Vector3f, 3> v={Eigen::Vector3f(_v[0].x(), _v[0].y(), 1.0f),
                                      Eigen::Vector3f(_v[1].x(), _v[1].y(), 1.0f),
                                      Eigen::Vector3f(_v[2].x(), _v[2].y(), 1.0f)};
    Eigen::Vector3f p(x, y, 1.0f);
    bool allPositive = true;
    bool allNegative = true;
    for(int i = 0; i < 3; ++i){
        Eigen::Vector3f edge = v[(i + 1) % 3] - v[i];
        Eigen::Vector3f toPoint = p - v[i];
        if(edge.cross(toPoint).z() < 0){
            allPositive = false;
        }else if(edge.cross(toPoint).z() > 0){
            allNegative = false;
        }
    }

    return allPositive || allNegative;
}

static std::tuple<float, float, float> computeBarycentric2D(float x, float y, const Vector3f* v)
{
    float c1 = (x*(v[1].y() - v[2].y()) + (v[2].x() - v[1].x())*y + v[1].x()*v[2].y() - v[2].x()*v[1].y()) / (v[0].x()*(v[1].y() - v[2].y()) + (v[2].x() - v[1].x())*v[0].y() + v[1].x()*v[2].y() - v[2].x()*v[1].y());
    float c2 = (x*(v[2].y() - v[0].y()) + (v[0].x() - v[2].x())*y + v[2].x()*v[0].y() - v[0].x()*v[2].y()) / (v[1].x()*(v[2].y() - v[0].y()) + (v[0].x() - v[2].x())*v[1].y() + v[2].x()*v[0].y() - v[0].x()*v[2].y());
    float c3 = (x*(v[0].y() - v[1].y()) + (v[1].x() - v[0].x())*y + v[0].x()*v[1].y() - v[1].x()*v[0].y()) / (v[2].x()*(v[0].y() - v[1].y()) + (v[1].x() - v[0].x())*v[2].y() + v[0].x()*v[1].y() - v[1].x()*v[0].y());
    return {c1,c2,c3};
}

void rst::rasterizer::rasterize_triangle(const Triangle& t){
    int x_min = std::floor(std::min({t.screen_pos[0].x(), t.screen_pos[1].x(), t.screen_pos[2].x()}));
    int x_max = std::ceil(std::max({t.screen_pos[0].x(), t.screen_pos[1].x(), t.screen_pos[2].x()}));
    int y_min = std::floor(std::min({t.screen_pos[0].y(), t.screen_pos[1].y(), t.screen_pos[2].y()}));
    int y_max = std::ceil(std::max({t.screen_pos[0].y(), t.screen_pos[1].y(), t.screen_pos[2].y()}));

    for(int i = x_min; i <= x_max; ++i){
        for(int j = y_min; j <= y_max; ++j){
            if(insideTriangle(i + 0.5f, j + 0.5f, t.screen_pos)){

                //Barycentric interpolation for color and depth
                auto[alpha, beta, gamma] = computeBarycentric2D(i + 0.5f, j + 0.5f, t.screen_pos);
                float z_interpolated = alpha * t.screen_pos[0].z() + beta * t.screen_pos[1].z() + gamma * t.screen_pos[2].z();
                
                int index = get_index(i, j);
                if(z_interpolated < depth_buf[index]){
                    depth_buf[index] = z_interpolated;
                    float w_reciprocal = 1.0f / (alpha * t.inv_w[0] + beta * t.inv_w[1] + gamma * t.inv_w[2]);
                    Eigen::Vector3f c_interpolated = alpha * t.color[0] * t.inv_w[0] + beta * t.color[1] * t.inv_w[1] + gamma * t.color[2] * t.inv_w[2];
                    c_interpolated *= w_reciprocal;
                    rst::rasterizer::set_pixel(Eigen::Vector3f(i, j, 1.0), c_interpolated);
                }

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

rst::rasterizer::rasterizer(int w, int h) : width(w), height(h){
    if (w <= 0 || h <= 0) {
        throw std::invalid_argument("Width and height must be positive integers.");
    }
    frame_buf.resize(static_cast<std::size_t>(w * h));
    depth_buf.resize(static_cast<std::size_t>(w * h));
}

void rst::rasterizer::clear(Buffers buff){
    if((buff & Buffers::Color) == Buffers::Color){
        std::fill(frame_buf.begin(), frame_buf.end(), Eigen::Vector3f(0, 0, 0));
    }
    if((buff & Buffers::Depth) == Buffers::Depth){
        std::fill(depth_buf.begin(), depth_buf.end(), std::numeric_limits<float>::infinity());
    }
}

void rst::rasterizer::draw(pos_buf_id pos_buffer, col_buf_id col_buffer, ind_buf_id ind_buffer, Primitive type){
    auto& pos = pos_buf[pos_buffer.pos_id];
    auto& col = col_buf[col_buffer.col_id];
    auto& ind = ind_buf[ind_buffer.ind_id];

    std::vector<Triangle> triangle_list;
    for(const auto& i: ind){
        Triangle t;
        for(int j = 0; j < 3; ++j){
            t.setVertex(j, pos[i[j]]);
            //color is not implemented yet, so we just use the color of the first vertex
            t.setColor(j, col[i[j]].x(), col[i[j]].y(), col[i[j]].z());
        }

        Eigen::Matrix4f mvp = projection * view * model;
        std::array<Eigen::Vector4f, 3> v = {
            (mvp * t.toVector4f()[0]),
            (mvp * t.toVector4f()[1]),
            (mvp * t.toVector4f()[2])
        };

        std::transform(std::begin(v), std::end(v), std::begin(t.inv_w), [](auto& vec){
            return 1.0f / vec.w();
        });

        std::transform(std::begin(v), std::end(v), std::begin(v), [](auto& vec){
            return Eigen::Vector4f(vec.x()/vec.w(), vec.y()/vec.w(), vec.z()/vec.w(), 1.0f);
        });
        std::transform(std::begin(v), std::end(v), std::begin(v), [this](auto& vec){
            return Eigen::Vector4f(0.5f * width * (vec.x() + 1.0f), 0.5f * height * (vec.y() + 1.0f), vec.z(), 1.0f);
        });

        for(int j = 0; j < 3; ++j){
            t.setScreenPos(j, Eigen::Vector3f(v[j].x(), v[j].y(), v[j].z()));
        }

        triangle_list.push_back(t);
    }

    if(type == Primitive::Triangle){
        for(const auto& t: triangle_list){
            rasterize_triangle(t);
        }
    }else if (type == Primitive::Line){
        for(const auto& t: triangle_list){
            draw_line(t.screen_pos[0], t.screen_pos[1]);
            draw_line(t.screen_pos[1], t.screen_pos[2]);
            draw_line(t.screen_pos[2], t.screen_pos[0]);
        }
    }
}
