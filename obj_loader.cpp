#include "obj_loader.hpp"

#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

[[noreturn]] void fail(std::size_t line, const std::string& message)
{
    throw std::runtime_error("OBJ line " + std::to_string(line) + ": " + message);
}

int position_index(const std::string& corner, std::size_t vertex_count, std::size_t line)
{
    // Each face corner can be v, v/vt, v//vn or v/vt/vn. Only v is needed here.
    const std::string token = corner.substr(0, corner.find('/'));
    long long raw_index = 0;
    std::size_t consumed = 0;
    try {
        raw_index = std::stoll(token, &consumed);
    } catch (const std::exception&) {
        fail(line, "invalid position index '" + corner + "'");
    }
    if (consumed != token.size() || raw_index == 0) {
        fail(line, "invalid position index '" + corner + "'");
    }

    // Negative indices refer to the positions defined before this face.
    const long long index = raw_index > 0 ? raw_index - 1
                                         : static_cast<long long>(vertex_count) + raw_index;
    if (index < 0 || index >= static_cast<long long>(vertex_count)) {
        fail(line, "position index '" + corner + "' is out of range");
    }
    return static_cast<int>(index);
}

double orient(const Eigen::Vector2d& a, const Eigen::Vector2d& b,
              const Eigen::Vector2d& c)
{
    return (b.x() - a.x()) * (c.y() - a.y()) -
           (b.y() - a.y()) * (c.x() - a.x());
}

bool on_segment(const Eigen::Vector2d& p, const Eigen::Vector2d& a,
                const Eigen::Vector2d& b, double epsilon)
{
    return std::abs(orient(a, b, p)) <= epsilon &&
           p.x() >= std::min(a.x(), b.x()) - epsilon &&
           p.x() <= std::max(a.x(), b.x()) + epsilon &&
           p.y() >= std::min(a.y(), b.y()) - epsilon &&
           p.y() <= std::max(a.y(), b.y()) + epsilon;
}

bool segments_intersect(const Eigen::Vector2d& a, const Eigen::Vector2d& b,
                        const Eigen::Vector2d& c, const Eigen::Vector2d& d,
                        double epsilon)
{
    const double ab_c = orient(a, b, c);
    const double ab_d = orient(a, b, d);
    const double cd_a = orient(c, d, a);
    const double cd_b = orient(c, d, b);
    const auto opposite = [epsilon](double x, double y) {
        return (x > epsilon && y < -epsilon) || (x < -epsilon && y > epsilon);
    };
    return (opposite(ab_c, ab_d) && opposite(cd_a, cd_b)) ||
           on_segment(c, a, b, epsilon) || on_segment(d, a, b, epsilon) ||
           on_segment(a, c, d, epsilon) || on_segment(b, c, d, epsilon);
}

void triangulate(std::vector<int> face, mesh::MeshData& mesh, std::size_t line)
{
    // Accept exporters that repeat the first index to close a polygon.
    if (face.size() > 3 && face.front() == face.back()) {
        face.pop_back();
    }
    if (face.size() < 3) {
        fail(line, "a face needs at least three vertices");
    }

    const Eigen::Vector3d origin = mesh.positions[face.front()].cast<double>();
    Eigen::Vector3d normal = Eigen::Vector3d::Zero();
    double extent = 0.0;
    for (std::size_t i = 0; i < face.size(); ++i) {
        const Eigen::Vector3d a = mesh.positions[face[i]].cast<double>() - origin;
        const Eigen::Vector3d b =
            mesh.positions[face[(i + 1) % face.size()]].cast<double>() - origin;
        normal += a.cross(b);
        extent = std::max(extent, a.norm());
    }
    if (normal.squaredNorm() == 0.0) {
        fail(line, "degenerate face or self-intersecting polygon");
    }
    if (face.size() == 3) {
        mesh.indices.emplace_back(face[0], face[1], face[2]);
        return;
    }

    // Project onto the plane with the largest area. Work in double precision
    // and normalize the coordinates so the tolerance is independent of scale.
    Eigen::Index dropped_axis = 0;
    normal.cwiseAbs().maxCoeff(&dropped_axis);
    const Eigen::Index u = (dropped_axis + 1) % 3;
    const Eigen::Index v = (dropped_axis + 2) % 3;
    const Eigen::Vector3d unit_normal = normal.normalized();
    std::vector<Eigen::Vector2d> points;
    points.reserve(face.size());
    for (int index : face) {
        const Eigen::Vector3d p = mesh.positions[index].cast<double>() - origin;
        if (std::abs(p.dot(unit_normal)) > extent * 1e-5) {
            fail(line, "non-planar polygon; triangulate it before export");
        }
        points.emplace_back(p[u] / extent, p[v] / extent);
    }

    constexpr double epsilon = 1e-12;
    double signed_area = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const std::size_t next = (i + 1) % points.size();
        if ((points[i] - points[next]).squaredNorm() <= epsilon * epsilon) {
            fail(line, "polygon contains a zero-length edge");
        }
        signed_area += orient(Eigen::Vector2d::Zero(), points[i], points[next]);
        for (std::size_t j = i + 1; j < points.size(); ++j) {
            const std::size_t j_next = (j + 1) % points.size();
            if (next == j || j_next == i) {
                continue; // Adjacent edges share an endpoint.
            }
            if (segments_intersect(points[i], points[next], points[j], points[j_next], epsilon)) {
                fail(line, "self-intersecting polygon");
            }
        }
    }
    if (std::abs(signed_area) <= epsilon) {
        fail(line, "degenerate polygon");
    }
    const double winding = signed_area > 0.0 ? 1.0 : -1.0;

    // Ear clipping: remove a convex corner only when its triangle contains no
    // other remaining vertex. This also handles concave polygons.
    std::vector<std::size_t> remaining(face.size());
    std::iota(remaining.begin(), remaining.end(), std::size_t{0});
    while (remaining.size() > 3) {
        bool clipped = false;
        for (std::size_t i = 0; i < remaining.size(); ++i) {
            const auto a = remaining[(i + remaining.size() - 1) % remaining.size()];
            const auto b = remaining[i];
            const auto c = remaining[(i + 1) % remaining.size()];
            if (winding * orient(points[a], points[b], points[c]) <= epsilon) {
                continue;
            }
            const bool contains_vertex = std::any_of(
                remaining.begin(), remaining.end(), [&](std::size_t p) {
                    return p != a && p != b && p != c &&
                           winding * orient(points[a], points[b], points[p]) >= -epsilon &&
                           winding * orient(points[b], points[c], points[p]) >= -epsilon &&
                           winding * orient(points[c], points[a], points[p]) >= -epsilon;
                });
            if (!contains_vertex) {
                mesh.indices.emplace_back(face[a], face[b], face[c]);
                remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
                clipped = true;
                break;
            }
        }
        if (!clipped) {
            fail(line, "cannot triangulate degenerate polygon");
        }
    }
    if (winding * orient(points[remaining[0]], points[remaining[1]], points[remaining[2]]) <= epsilon) {
        fail(line, "triangulation produced a degenerate triangle");
    }
    mesh.indices.emplace_back(face[remaining[0]], face[remaining[1]], face[remaining[2]]);
}

void read_vertex(std::istringstream& record, mesh::MeshData& mesh,
                 const Eigen::Vector3f& default_color, std::size_t line)
{
    std::vector<float> values;
    float value = 0.0f;
    record >> std::ws;
    while (!record.eof()) {
        if (!(record >> value) || !std::isfinite(value)) {
            fail(line, "vertex components must be finite numbers");
        }
        values.push_back(value);
        record >> std::ws;
    }
    if (values.size() != 3 && values.size() != 4 && values.size() != 6 && values.size() != 7) {
        fail(line, "expected v x y z [w] or v x y z [w] r g b");
    }
    if (mesh.positions.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        fail(line, "too many vertices for the rasterizer's integer indices");
    }

    // The optional w is a free-form vertex weight; polygon positions use xyz.
    Eigen::Vector3f color = default_color;
    if (values.size() >= 6) {
        const std::size_t start = values.size() - 3;
        color = Eigen::Vector3f(values[start], values[start + 1], values[start + 2]);
        if ((color.array() < 0.0f).any() || (color.array() > 1.0f).any()) {
            fail(line, "OBJ vertex RGB values must be in [0, 1]");
        }
        color *= 255.0f;
    }
    mesh.positions.emplace_back(values[0], values[1], values[2]);
    mesh.colors.push_back(color);
}

} // namespace

mesh::MeshData mesh::parse_obj(std::istream& input, const Eigen::Vector3f& default_color)
{
    if (!default_color.allFinite() || (default_color.array() < 0.0f).any() ||
        (default_color.array() > 255.0f).any()) {
        throw std::invalid_argument("Default vertex color must be finite and in [0, 255]");
    }

    MeshData result;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        const std::size_t record_line = ++line_number;
        if (line_number == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) {
            line.erase(0, 3); // UTF-8 BOM.
        }
        std::string text;
        while (true) {
            const auto comment = line.find('#');
            if (comment != std::string::npos) {
                line.resize(comment);
            }
            const auto last = line.find_last_not_of(" \t\r");
            const bool continued = last != std::string::npos && line[last] == '\\';
            if (continued) {
                line.resize(last);
            }
            text += line;
            if (!continued) {
                break;
            }
            text += ' ';
            if (!std::getline(input, line)) {
                fail(record_line, "unfinished line continuation");
            }
            ++line_number;
        }

        std::istringstream record(text);
        record.imbue(std::locale::classic());
        std::string type;
        record >> type;
        if (type == "v") {
            read_vertex(record, result, default_color, record_line);
        } else if (type == "f") {
            std::vector<int> face;
            std::string corner;
            while (record >> corner) {
                face.push_back(position_index(corner, result.positions.size(), record_line));
            }
            triangulate(std::move(face), result, record_line);
        }
    }
    if (input.bad()) {
        throw std::runtime_error("Failed to read OBJ stream");
    }
    if (result.indices.empty()) {
        throw std::runtime_error("OBJ contains no polygon faces");
    }
    return result;
}

mesh::MeshData mesh::load_obj(const std::filesystem::path& path,
                             const Eigen::Vector3f& default_color)
{
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot open OBJ file '" + path.string() + "'");
    }
    try {
        return parse_obj(input, default_color);
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}
