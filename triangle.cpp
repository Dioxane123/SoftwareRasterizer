#include "triangle.hpp"
#include <array>
#include <algorithm>
#include <stdexcept>

Triangle::Triangle(){
    v[0] << 0, 0, 0;
    v[1] << 0, 0, 0;
    v[2] << 0, 0, 0;

    color[0] << 0, 0, 0;
    color[1] << 0, 0, 0;
    color[2] << 0, 0, 0;

    tex_coord[0] << 0, 0;
    tex_coord[1] << 0, 0;
    tex_coord[2] << 0, 0;
}

void Triangle::setVertex(int idx, Vector3f vertex){ v[idx] = vertex; }
void Triangle::setNormal(int idx, Vector3f normal){ n[idx] = normal; }
void Triangle::setColor(int idx, float r, float g, float b){
    if(r < 0.0 || r > 255.0 || g < 0.0 || g > 255.0 || b < 0.0 || b > 255.0){
        throw std::invalid_argument("Color values must be in the range [0, 255]");
    }

    color[idx] << Vector3f(r / 255.0, g / 255.0, b / 255.0);
    return;
}
void Triangle::setTexCoord(int idx, float s, float t){ tex_coord[idx] << Vector2f(s, t); }
std::array<Vector4f, 3> Triangle::toVector4f() const {
    std::array<Vector4f, 3> res;
    std::transform(std::begin(v), std::end(v), std::begin(res), [](auto& vec){
        return Vector4f(vec.x(), vec.y(), vec.z(), 1.0f);
    });
    return res;
}
