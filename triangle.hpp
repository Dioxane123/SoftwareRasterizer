#ifndef TRIANGLE_HPP
#define TRIANGLE_HPP

#include <eigen3/Eigen/Eigen>

using namespace Eigen;
class Triangle{
    public:
        Vector3f v[3];
        Vector3f color[3];
        Vector2f tex_coord[3];
        Vector3f n[3];

        Triangle();

        Eigen::Vector3f a() const { return v[0]; }
        Eigen::Vector3f b() const { return v[1]; }
        Eigen::Vector3f c() const { return v[2]; }

        void setVertex(int idx, Vector3f vertex);
        void setColor(int idx, float r, float g, float b);
        void setTexCoord(int idx, float s, float t);
        void setNormal(int idx, Vector3f normal);
        std::array<Vector4f, 3> toVector4f() const;

};

#endif // TRIANGLE_HPP