#include "obj_loader.hpp"
#include "rasterizer.hpp"

#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

mesh::MeshData parse(const std::string& text,
                     const Eigen::Vector3f& color = Eigen::Vector3f(180, 180, 180))
{
    std::istringstream input(text);
    return mesh::parse_obj(input, color);
}

void expect_error(const std::string& text, const std::string& expected)
{
    try {
        parse(text);
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()).find(expected) != std::string::npos,
                "Unexpected error: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("Invalid OBJ was accepted: " + text);
}

void test_colors_and_face_references()
{
    const Eigen::Vector3f fallback(12, 34, 56);
    const auto data = parse(
        "o ColoredTriangle\n"
        "v 0 0 0 1 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0 0 0.5 1\n"
        "vt 0 0\nvt 1 0\nvn 0 0 1\n"
        "usemtl ignored_material\ns 1\n"
        "f 1/1/1 2//1 3/2\n", fallback);
    require(data.positions.size() == 3 && data.indices.size() == 1 && data.colors.size() == 3,
            "Triangle array sizes are incorrect");
    require(data.indices[0] == Eigen::Vector3i(0, 1, 2), "Face indices must be zero-based");
    require(data.positions[1] == Eigen::Vector3f(1, 0, 0), "Position was changed");
    require(data.colors[0].isApprox(Eigen::Vector3f(255, 0, 0)), "RGB conversion failed");
    require(data.colors[1].isApprox(fallback), "Missing RGB did not use the default");
    require(data.colors[2].isApprox(Eigen::Vector3f(0, 127.5f, 255)), "RGB precision was lost");
}

void test_relative_indices_and_text_formats()
{
    const auto data = parse(
        "\xef\xbb\xbf# UTF-8 BOM and CRLF\r\n"
        "\tv 0 0 0 1\r\n"
        "v 1 0 0\r\n"
        "v 1 1 0\r\n"
        "v 0 1 0\r\n"
        "f -4 -3 \\\r\n -2 -1 -4 # repeat first corner\r\n"
        "v 10 10 10\n");
    require(data.positions.size() == 5 && data.colors.size() == 5 && data.indices.size() == 2,
            "Quad, continuation or optional weight parsing failed");
    for (const auto& triangle : data.indices) {
        require(triangle.minCoeff() >= 0 && triangle.maxCoeff() < 4,
                "Relative indices must use the vertex count at the face record");
    }
}

void test_concave_polygons()
{
    // L-shaped polygon: the upper-right 2x2 square must remain empty.
    for (bool reversed : {false, true}) {
        for (bool vertical : {false, true}) {
            std::ostringstream obj;
            const Eigen::Vector2f outline[] = {{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}};
            for (const auto& p : outline) {
                if (vertical) {
                    obj << "v " << p.x() << " 2 " << p.y() << '\n';
                } else {
                    obj << "v " << p.x() << ' ' << p.y() << " 0\n";
                }
            }
            obj << (reversed ? "f 6 5 4 3 2 1\n" : "f 1 2 3 4 5 6\n");
            const auto data = parse(obj.str());
            require(data.indices.size() == 4, "A six-corner polygon must produce four triangles");
            double total_area = 0.0;
            const auto project = [vertical](const Eigen::Vector3f& p) {
                return Eigen::Vector2f(p.x(), vertical ? p.z() : p.y());
            };
            for (const auto& triangle : data.indices) {
                const Eigen::Vector2f a = project(data.positions[triangle[0]]);
                const Eigen::Vector2f b = project(data.positions[triangle[1]]);
                const Eigen::Vector2f c = project(data.positions[triangle[2]]);
                const double twice_area = (b.x() - a.x()) * (c.y() - a.y()) -
                                          (b.y() - a.y()) * (c.x() - a.x());
                require(reversed ? twice_area < 0 : twice_area > 0, "Polygon winding changed");
                total_area += std::abs(twice_area) * 0.5;
                const Eigen::Vector2f center = (a + b + c) / 3.0f;
                require(!(center.x() > 1 && center.y() > 1), "Triangle fills the concave notch");
            }
            require(std::abs(total_area - 5.0) < 1e-6, "Triangulation changed polygon area");
        }
    }

    const auto collinear = parse("v 0 0 0\nv 1 0 0\nv 2 0 0\nv 2 1 0\nv 0 1 0\nf 1 2 3 4 5\n");
    require(collinear.indices.size() == 3, "Collinear boundary vertices must be supported");
}

void test_invalid_data()
{
    const std::string vertices = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    for (const std::string& face : {"f 0 2 3", "f 1 2 4", "f -4 -2 -1", "f a 2 3",
                                    "f 1x 2 3", "f 999999999999999999999999 2 3",
                                    "f 1 2", "f 1 1 2", "f /1 2 3"}) {
        expect_error(vertices + face + "\n", "OBJ line 4:");
    }
    for (const std::string& vertex : {"v 0 0", "v 0 0 0 bad", "v 0 0 0 1e999",
                                      "v 0 0 0 nan 0 0", "v 0 0 0 2 0 0",
                                      "v 0 0 0 0 -1 0", "v 0 0 0 1 2"}) {
        expect_error(vertex + "\n", "OBJ line 1:");
    }
    expect_error("", "no polygon faces");
    expect_error(vertices, "no polygon faces");
    expect_error(vertices + "f 1 2 \\\n", "unfinished line continuation");
    expect_error("v 0 0 0\nv 2 2 0\nv 0 2 0\nv 2 0 0\nf 1 2 3 4\n", "OBJ line 5:");
    expect_error("v 0 0 0\nv 1 0 0\nv 1 1 1\nv 0 1 0\nf 1 2 3 4\n", "non-planar");
    for (const Eigen::Vector3f& color : {
             Eigen::Vector3f(-1, 0, 0), Eigen::Vector3f(256, 0, 0),
             Eigen::Vector3f(std::numeric_limits<float>::quiet_NaN(), 0, 0)}) {
        bool rejected = false;
        try {
            parse(vertices + "f 1 2 3\n", color);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "Invalid default color was accepted");
    }
}

mesh::MeshData test_cube(const std::filesystem::path& path)
{
    const auto cube = mesh::load_obj(path);
    require(cube.positions.size() == 8 && cube.indices.size() == 12 && cube.colors.size() == 8,
            "Cube must have 8 positions, 12 triangles and 8 colors");
    std::map<std::pair<int, int>, int> edges;
    double area = 0.0;
    for (const auto& triangle : cube.indices) {
        require(triangle.minCoeff() >= 0 && triangle.maxCoeff() < 8, "Cube index out of range");
        const Eigen::Vector3f a = cube.positions[triangle[0]];
        const Eigen::Vector3f b = cube.positions[triangle[1]];
        const Eigen::Vector3f c = cube.positions[triangle[2]];
        const Eigen::Vector3f normal = (b - a).cross(c - a);
        require(normal.dot((a + b + c) / 3.0f) > 0, "Cube triangle faces inward");
        area += normal.norm() * 0.5;
        for (int i = 0; i < 3; ++i) {
            ++edges[std::minmax(triangle[i], triangle[(i + 1) % 3])];
        }
    }
    require(std::abs(area - 24.0) < 1e-6, "Cube surface area is incorrect");
    for (const auto& edge : edges) {
        require(edge.second == 2, "Cube must be a closed mesh");
    }
    for (const auto& color : cube.colors) {
        require(color == Eigen::Vector3f(180, 180, 180), "Cube default color is incorrect");
    }
    try {
        mesh::load_obj(path / "missing.obj");
        throw std::logic_error("Missing file was accepted");
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()).find("missing.obj") != std::string::npos,
                "Missing-file error must include the path");
    }
    return cube;
}

void test_rendering(const mesh::MeshData& cube, const char* image_path)
{
    constexpr int size = 256;
    rst::rasterizer renderer(size, size);
    const auto positions = renderer.load_positions(cube.positions);
    const auto indices = renderer.load_indices(cube.indices);
    const auto colors = renderer.load_colors(cube.colors);

    Eigen::Matrix4f view = Eigen::Matrix4f::Identity();
    const Eigen::Vector3f eye(3, 2, 5);
    const Eigen::Vector3f back = eye.normalized();
    const Eigen::Vector3f right = Eigen::Vector3f::UnitY().cross(back).normalized();
    const Eigen::Vector3f up = back.cross(right);
    view.block<1, 3>(0, 0) = right.transpose();
    view.block<1, 3>(1, 0) = up.transpose();
    view.block<1, 3>(2, 0) = back.transpose();
    view.block<3, 1>(0, 3) = -view.block<3, 3>(0, 0) * eye;
    Eigen::Matrix4f projection = Eigen::Matrix4f::Zero();
    projection(0, 0) = projection(1, 1) = 1.0f / std::tan(3.14159265f / 8.0f);
    projection(2, 2) = -50.1f / 49.9f;
    projection(2, 3) = -10.0f / 49.9f;
    projection(3, 2) = -1.0f;
    renderer.set_model(Eigen::Matrix4f::Identity());
    renderer.set_view(view);
    renderer.set_projection(projection);
    renderer.clear(rst::Buffers::Color | rst::Buffers::Depth);
    renderer.draw(positions, colors, indices, Eigen::Vector3f(3, 4, 5), rst::Primitive::Triangle);

    std::size_t colored_pixels = 0;
    for (const auto& pixel : renderer.frame_buffer()) {
        require(pixel.allFinite(), "Rendering produced a non-finite color");
        colored_pixels += pixel.squaredNorm() > 0;
    }
    require(colored_pixels > 1000 && colored_pixels < size * size / 2,
            "Imported cube did not render at the expected scale");
    if (image_path != nullptr) {
        std::ofstream image(image_path);
        image << "P3\n" << size << ' ' << size << "\n255\n";
        for (const auto& pixel : renderer.frame_buffer()) {
            for (int channel = 0; channel < 3; ++channel) {
                image << static_cast<int>(std::clamp(pixel[channel], 0.0f, 1.0f) * 255 + 0.5f) << ' ';
            }
            image << '\n';
        }
        require(static_cast<bool>(image), "Cannot write rendering preview");
    }
    std::cout << "Cube: 8 positions, 12 triangles, 8 colors; " << colored_pixels << " rendered pixels.\n";
}

} // namespace

int main(int argc, char** argv)
{
    try {
        require(argc >= 2, "Expected the cube OBJ path");
        test_colors_and_face_references();
        test_relative_indices_and_text_formats();
        test_concave_polygons();
        test_invalid_data();
        const auto cube = test_cube(argv[1]);
        test_rendering(cube, argc > 2 ? argv[2] : nullptr);
        std::cout << "All OBJ loader and rasterizer integration tests passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
