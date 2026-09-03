#ifndef RASTERIZER_HPP
#define RASTERIZER_HPP

#include <eigen3/Eigen/Eigen>
#include <algorithm>
#include "triangle.hpp"
using namespace Eigen;

namespace rst {
    enum class Buffers{
        Color = 1,
        Depth = 2
    };

    inline Buffers operator|(Buffers a, Buffers b){
        return Buffers((int)a | (int)b);
    }

    inline Buffers operator&(Buffers a, Buffers b){
        return Buffers((int)a & (int)b);
    }

    enum class Primitive{
        Line,
        Triangle
    };

    //for type safety.
    typedef struct{
        int pos_id = 0;
    }pos_buf_id;

    typedef struct{
        int ind_id = 0;
    }ind_buf_id;
    
    class rasterizer{
        public:
            rasterizer(int width, int height);
            pos_buf_id load_positions(const std::vector<Eigen::Vector3f>& positions);
            ind_buf_id load_indices(const std::vector<Eigen::Vector3i>& indices);
            
            void set_model(const Eigen::Matrix4f& m);
            void set_view(const Eigen::Matrix4f& v);
            void set_projection(const Eigen::Matrix4f& p);

            void set_pixel(const Vector3f& point, const Vector3f& color);

            void clear(Buffers buffer);

            void draw(pos_buf_id pos_buffer, ind_buf_id ind_buffer, Primitive type);
        
        private:
            Eigen::Matrix4f model;
            Eigen::Matrix4f view;
            Eigen::Matrix4f projection;

            std::map<int, std::vector<Eigen::Vector3f>> pos_buf;
            std::map<int, std::vector<Eigen::Vector3i>> ind_buf;

            std::vector<Eigen::Vector3f> frame_buf;
            std::vector<float> depth_buf;

            int width, height;

            int next_id = 0;
            int get_next_id() { return next_id++; }
    };
}


#endif // RASTERIZER_HPP