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
    //copied from games101
    float c1 = (x*(v[1].y() - v[2].y()) + (v[2].x() - v[1].x())*y + v[1].x()*v[2].y() - v[2].x()*v[1].y()) / (v[0].x()*(v[1].y() - v[2].y()) + (v[2].x() - v[1].x())*v[0].y() + v[1].x()*v[2].y() - v[2].x()*v[1].y());
    float c2 = (x*(v[2].y() - v[0].y()) + (v[0].x() - v[2].x())*y + v[2].x()*v[0].y() - v[0].x()*v[2].y()) / (v[1].x()*(v[2].y() - v[0].y()) + (v[0].x() - v[2].x())*v[1].y() + v[2].x()*v[0].y() - v[0].x()*v[2].y());
    float c3 = (x*(v[0].y() - v[1].y()) + (v[1].x() - v[0].x())*y + v[0].x()*v[1].y() - v[1].x()*v[0].y()) / (v[2].x()*(v[0].y() - v[1].y()) + (v[1].x() - v[0].x())*v[2].y() + v[0].x()*v[1].y() - v[1].x()*v[0].y());
    return {c1,c2,c3};
}

static float powInt(float a, int n){
    float res = 1.0f;
    while(n > 0){
        if(n & 1)res *= a;
        a *= a;
        n >>= 1;
    }
    return res;
}

static Eigen::Vector3f shadeBlinnPhong(Eigen::Vector3f view_pos, Eigen::Vector3f n, 
                                        Eigen::Vector3f kd, Eigen::Vector3f light_view){
    Eigen::Vector3f L = (light_view - view_pos).normalized();
    Eigen::Vector3f V = (-view_pos).normalized();
    Eigen::Vector3f H = (V + L).normalized();

    Eigen::Vector3f I_ambient = Eigen::Vector3f(1.0f, 1.0f, 1.0f) * 0.1f;

    float n_dot_L = std::max(n.dot(L), 0.0f);
    Eigen::Vector3f ks(0.8f, 0.8f, 0.8f);
    Eigen::Vector3f I_diffuse = kd * 1.0 * n_dot_L;
    Eigen::Vector3f I_specular = ks * (n_dot_L > 0.0f ? 1.0 * powInt(std::max(n.dot(H), 0.0f), 8) : 0);

    return I_ambient + I_diffuse + I_specular;

}

void rst::rasterizer::rasterize_triangle(const Triangle& t, Eigen::Vector3f light_view){
    if((t.screen_pos[1].x() - t.screen_pos[0].x()) * (t.screen_pos[2].y() - t.screen_pos[0].y()) -
(t.screen_pos[1].y() - t.screen_pos[0].y()) * (t.screen_pos[2].x() - t.screen_pos[0].x()) < 2e-14f){
        return;
    }

    int x_min = std::floor(std::min({t.screen_pos[0].x(), t.screen_pos[1].x(), t.screen_pos[2].x()}));
    int x_max = std::ceil(std::max({t.screen_pos[0].x(), t.screen_pos[1].x(), t.screen_pos[2].x()}));
    int y_min = std::floor(std::min({t.screen_pos[0].y(), t.screen_pos[1].y(), t.screen_pos[2].y()}));
    int y_max = std::ceil(std::max({t.screen_pos[0].y(), t.screen_pos[1].y(), t.screen_pos[2].y()}));

    x_min = x_min > 0 ? x_min : 0;
    x_max = x_max < width - 1 ? x_max : width - 1;
    y_min = y_min > 0 ? y_min : 0;
    y_max = y_max < height - 1 ? y_max : height - 1;

    for(int i = x_min; i <= x_max; ++i){
        for(int j = y_min; j <= y_max; ++j){
            if(insideTriangle(i + 0.5f, j + 0.5f, t.screen_pos)){

                // Barycentric interpolation for depth(screen space)
                auto[alpha, beta, gamma] = computeBarycentric2D(i + 0.5f, j + 0.5f, t.screen_pos);
                float z_interpolated = alpha * t.screen_pos[0].z() + beta * t.screen_pos[1].z() +
                                        gamma * t.screen_pos[2].z();
                
                int index = get_index(i, j);
                if(z_interpolated < depth_buf[index]){
                    depth_buf[index] = z_interpolated;

                    float w_reciprocal = 1.0f / 
                                        (alpha * t.inv_w[0] + beta * t.inv_w[1] + 
                                        gamma * t.inv_w[2]);

                    // barycentric interpolation for color
                    Eigen::Vector3f c_interpolated = alpha * t.color[0] * t.inv_w[0] + 
                                    beta * t.color[1] * t.inv_w[1] + gamma * t.color[2] * t.inv_w[2];
                    c_interpolated *= w_reciprocal;

                    // barycentric interpolation for normal
                    Eigen::Vector3f n_interpolated = alpha * t.n[0] * t.inv_w[0] +
                                    beta * t.n[1] * t.inv_w[1] + gamma * t.n[2] * t.inv_w[2];
                    n_interpolated *= w_reciprocal;
                    n_interpolated.normalize();
                    
                    // barycentric interpolation for coordinates(view space)
                    Eigen::Vector3f view_pos = alpha * t.v[0] * t.inv_w[0] + 
                                    beta * t.v[1] * t.inv_w[1] + gamma * t.v[2] * t.inv_w[2];
                    view_pos *= w_reciprocal;


                    rst::rasterizer::set_pixel(Eigen::Vector3f(i, j, 1.0), 
                                shadeBlinnPhong(view_pos, n_interpolated, c_interpolated, light_view));
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

template<typename T>
static T interpolateVertex(const T& a, const T& b, float t){
    return (1 - t) * a + t * b;
}

namespace{
    struct triangleElem{
        std::vector<Eigen::Vector4f> v;
        std::vector<Eigen::Vector3f> col;
        std::vector<Eigen::Vector3f> n;
    };
}

static triangleElem polyAgainstPlane(const triangleElem& input, const Eigen::Vector4f& plane){
    triangleElem output;

    if(input.v.empty())return output;

    int S = input.v.size() - 1;
    for(int E = 0; E < input.v.size(); ++E){
        float DS = input.v[S].dot(plane);
        float DE = input.v[E].dot(plane);
        if((DS > 0 && DE < 0) || (DS < 0 && DE > 0)){ // maybe have float percision bug
            float t = DS / (DS - DE);
            output.v.push_back(interpolateVertex(input.v[S], input.v[E], t));
            output.col.push_back(interpolateVertex(input.col[S], input.col[E], t));
            output.n.push_back(interpolateVertex(input.n[S], input.n[E], t));
        }
        if(DE >= 0){
            output.v.push_back(input.v[E]);
            output.col.push_back(input.col[E]);
            output.n.push_back(input.n[E]);
        }
        S = E;
    } 

    return output;

}

void rst::rasterizer::draw(pos_buf_id pos_buffer, col_buf_id col_buffer,
                            ind_buf_id ind_buffer, Eigen::Vector3f light_pos, Primitive type){
    auto& pos = pos_buf[pos_buffer.pos_id];
    auto& col = col_buf[col_buffer.col_id];
    auto& ind = ind_buf[ind_buffer.ind_id];

    std::vector<Triangle> triangle_list;
    for(const auto& i: ind){
        Triangle t;
        
        Eigen::Vector3f normal = (pos[i[1]] - pos[i[0]]).cross(pos[i[2]] - pos[i[0]]);
        Eigen::Matrix4f mv = view * model;
        if(normal.squaredNorm() < 1e-12f){
            normal = Eigen::Vector3f(0, 0, 0);
        }else{
            Eigen::Matrix3f normalMatrix = mv.block<3, 3>(0, 0).inverse().transpose();
            Eigen::Vector3f normalView = (normalMatrix * normal).normalized();
            normal = normalView;
        }

        for(int j = 0; j < 3; ++j){
            t.setVertex(j, (mv * pos[i[j]].homogeneous()).head<3>());
            t.setColor(j, col[i[j]].x(), col[i[j]].y(), col[i[j]].z());
            t.setNormal(j, normal);
        }

        std::array<Eigen::Vector4f, 3> v = t.toVector4f();
        for(auto& vertex: v){
            vertex = projection * vertex;
        }

        triangleElem input;
        for(int i = 0; i < 3; ++i){
            input.v.push_back(v[i]);
            input.col.push_back(t.color[i]);
            input.n.push_back(t.n[i]);
        }

        input = polyAgainstPlane(input, Eigen::Vector4f(0.0f, 0.0f, 1.0f, 1.0f));// z-near
        input = polyAgainstPlane(input, Eigen::Vector4f(0.0f, 0.0f, -1.0f, 1.0f));// z-far
        input = polyAgainstPlane(input, Eigen::Vector4f(1.0f, 0.0f, 0.0f, 1.0f));// left plane
        input = polyAgainstPlane(input, Eigen::Vector4f(-1.0f, 0.0f, 0.0f, 1.0f));// right plane
        input = polyAgainstPlane(input, Eigen::Vector4f(0.0f, -1.0f, 0.0f, 1.0f));// top plane
        input = polyAgainstPlane(input, Eigen::Vector4f(0.0f, 1.0f, 0.0f, 1.0f));// bottom plane
        Eigen::Matrix4f proj_inv = projection.inverse();

        if(input.v.size() < 3)continue;
        for(int i = 0; i < input.v.size()-2; ++i){
            Triangle tri;
            tri.setVertex(0, (proj_inv * input.v[0]).head<3>());
            tri.setColorNorm(0, input.col[0].x(), input.col[0].y(), input.col[0].z());
            tri.setNormal(0, input.n[0]);
            tri.inv_w[0] = 1.0f / input.v[0].w();
            Eigen::Vector4f SP = input.v[0]; //Screen Position
            SP << SP.x()/SP.w(), SP.y()/SP.w(), SP.z()/SP.w(), 1.0f;
            SP << 0.5f * width * (SP.x() + 1.0f), 0.5f * height * (SP.y() + 1.0f), SP.z(), 1.0f; 
            tri.setScreenPos(0, SP.head<3>());
            for(int j = i + 1; j < i + 3; ++j){
                tri.setVertex(j-i, (proj_inv * input.v[j]).head<3>());
                tri.setColorNorm(j-i, input.col[j].x(), input.col[j].y(), input.col[j].z());
                tri.setNormal(j-i, input.n[j]);
                tri.inv_w[j-i] = 1.0f / input.v[j].w();
                Eigen::Vector4f SP = input.v[j]; //Screen Position
                SP << SP.x()/SP.w(), SP.y()/SP.w(), SP.z()/SP.w(), 1.0f;
                SP << 0.5f * width * (SP.x() + 1.0f), 0.5f * height * (SP.y() + 1.0f), SP.z(), 1.0f; 
                tri.setScreenPos(j-i, SP.head<3>());
            }
            triangle_list.push_back(tri);
        }
    }

    if(type == Primitive::Triangle){
        // coordinate of light in view space
        Eigen::Vector3f light_view = (view * light_pos.homogeneous()).head<3>();

        for(const auto& t: triangle_list){
            rasterize_triangle(t, light_view);
        }
    }else if (type == Primitive::Line){
        for(const auto& t: triangle_list){
            draw_line(t.screen_pos[0], t.screen_pos[1]);
            draw_line(t.screen_pos[1], t.screen_pos[2]);
            draw_line(t.screen_pos[2], t.screen_pos[0]);
        }
    }
}
