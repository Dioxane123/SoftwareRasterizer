#ifndef OBJ_LOADER_HPP
#define OBJ_LOADER_HPP

#include <Eigen/Core>
#include <filesystem>
#include <iosfwd>
#include <vector>

namespace mesh {

// These arrays can be passed directly to rasterizer::load_positions(),
// load_indices() and load_colors(). Indices are zero-based; colors are [0, 255].
struct MeshData {
    std::vector<Eigen::Vector3f> positions;
    std::vector<Eigen::Vector3i> indices;
    std::vector<Eigen::Vector3f> colors;
};

// Supports polygon meshes with v and f records, including relative indices and
// v/vt/vn face references. UVs, normals, materials and free-form data are ignored.
// Optional vertex RGB values use [0, 1]; missing colors use default_color.
// Simple planar polygons are triangulated without changing their winding.
// Invalid geometry produces std::runtime_error with a source line number.
MeshData parse_obj(std::istream& input,
                   const Eigen::Vector3f& default_color = Eigen::Vector3f(180, 180, 180));

MeshData load_obj(const std::filesystem::path& path,
                  const Eigen::Vector3f& default_color = Eigen::Vector3f(180, 180, 180));

} // namespace mesh

#endif // OBJ_LOADER_HPP
