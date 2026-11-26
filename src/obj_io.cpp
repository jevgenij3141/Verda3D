#include "obj_io.h"
#include "scene_mesh.h"
#include "text_encoding.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace {
constexpr std::size_t maxVertices = 100000, maxFaces = 100000;

bool indexOf(std::string_view text, std::size_t count, std::size_t& index) {
    long long raw = 0;
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    const auto result = std::from_chars(text.data(), text.data() + text.size(), raw);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || raw == 0) return false;
    const auto offset = raw < 0 ? static_cast<long long>(count) + raw : raw - 1;
    if (offset < 0 || offset >= static_cast<long long>(count)) return false;
    index = static_cast<std::size_t>(offset);
    return true;
}

double turn(Vec2 a, Vec2 b, Vec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool appendPolygon(Mesh& mesh, const std::vector<std::size_t>& vertices, const std::vector<Vec2>& uv) {
    const auto append = [&](std::size_t a, std::size_t b, std::size_t c) {
        mesh.faces.push_back({{vertices[a], vertices[b], vertices[c], vertices[c]}, {uv[a], uv[b], uv[c], uv[c]}, 3});
    };
    const Vec3 origin = mesh.vertices[vertices.front()];
    const auto p = [&](std::size_t i) { return mesh.vertices[vertices[i]] - origin; };
    Vec3 normal{};
    for (std::size_t i = 1; i + 1 < vertices.size(); ++i) normal = normal + cross(p(i), p(i + 1));
    if (dot(normal, normal) <= 1e-20) return false;
    if (vertices.size() == 3) {
        append(0, 1, 2);
        return true;
    }
    if (vertices.size() == 4) {
        const Vec3 leftover = p(2) - p(1) - p(3);
        const Vec2 uvLeftover = uv[0] + uv[2] - uv[1] - uv[3];
        if (dot(leftover, leftover) <= 1e-18 * std::max(1.0, dot(p(2), p(2))) &&
            dot(uvLeftover, uvLeftover) <= 1e-18) {
            mesh.faces.push_back({{vertices[0], vertices[1], vertices[2], vertices[3]}, {uv[0], uv[1], uv[2], uv[3]}});
            return true;
        }
    }
    const Vec3 n = normalized(normal);
    const Vec3 right = normalized(cross(std::abs(n.y) > .95 ? axes[0] : axes[1], n));
    const Vec3 up = cross(n, right);
    std::vector<Vec2> projected;
    for (std::size_t i = 0; i < vertices.size(); ++i) projected.push_back({dot(p(i), right), dot(p(i), up)});
    std::vector<std::size_t> remaining(vertices.size());
    std::iota(remaining.begin(), remaining.end(), 0);
    constexpr double tolerance = 1e-12;
    while (remaining.size() > 3) {
        bool changed = false;
        for (std::size_t i = 0; i < remaining.size(); ++i) {
            const auto a = remaining[(i + remaining.size() - 1) % remaining.size()];
            const auto b = remaining[i], c = remaining[(i + 1) % remaining.size()];
            const double area = turn(projected[a], projected[b], projected[c]);
            if (std::abs(area) <= tolerance) {
                remaining.erase(remaining.begin() + i);
                changed = true;
                break;
            }
            if (area < 0) continue;
            const auto overlaps = [&](std::size_t point) {
                if (point == a || point == b || point == c) return false;
                const Vec2 q = projected[point];
                return turn(projected[a], projected[b], q) >= -tolerance &&
                       turn(projected[b], projected[c], q) >= -tolerance &&
                       turn(projected[c], projected[a], q) >= -tolerance;
            };
            if (std::any_of(remaining.begin(), remaining.end(), overlaps)) continue;
            append(a, b, c);
            remaining.erase(remaining.begin() + i);
            changed = true;
            break;
        }
        if (!changed) return false;
    }
    if (turn(projected[remaining[0]], projected[remaining[1]], projected[remaining[2]]) <= tolerance) return false;
    append(remaining[0], remaining[1], remaining[2]);
    return mesh.faces.size() <= maxFaces;
}

std::string cleanName(std::string name, std::size_t number) {
    const auto first = name.find_first_not_of(" \t\r");
    if (first == std::string::npos) return "Mesh " + std::to_string(number);
    name = name.substr(first, name.find_last_not_of(" \t\r") - first + 1);
    name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == '#'; }), name.end());
    if (name.size() > 128) {
        std::size_t end = 128;
        while (end > 0 && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80) --end;
        name.resize(end);
    }
    return name.empty() ? "Mesh " + std::to_string(number) : name;
}

void recenter(Model& model) {
    const Vec3 center = modelBounds(model).center();
    for (Vec3& vertex : model.mesh.vertices) vertex = vertex - center;
    model.transform.position = center;
}
} // namespace

bool importObj(const wchar_t* filename, std::vector<Model>& models, std::wstring& error) {
    try {
        const std::filesystem::path path(filename);
        if (std::filesystem::file_size(path) > 64ull * 1024 * 1024) {
            error = L"The OBJ file exceeds 64 MB.";
            return false;
        }
        std::ifstream input(path);
        std::vector<Vec3> positions;
        std::vector<Vec2> coordinates;
        std::vector<Model> draft;
        std::unordered_map<std::size_t, std::size_t> remap;
        Model object{};
        object.kind = ObjectKind::Mesh;
        object.name = cleanName(narrowUtf8(path.stem().wstring()), 1);
        const auto finish = [&] {
            if (object.mesh.faces.empty()) return;
            recenter(object);
            draft.push_back(std::move(object));
            object = {};
            object.kind = ObjectKind::Mesh;
            remap.clear();
        };
        std::string line;
        unsigned lineNumber = 0;
        const auto invalid = [&] {
            error = L"Invalid or unsupported geometry at OBJ line " + std::to_wstring(lineNumber) + L".";
            return false;
        };
        while (std::getline(input, line)) {
            ++lineNumber;
            if (line.size() > 1024 * 1024) return invalid();
            if (lineNumber == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
            const auto comment = line.find('#');
            if (comment != std::string::npos) line.resize(comment);
            std::istringstream row(line);
            row.imbue(std::locale::classic());
            std::string type;
            row >> type;
            if (type == "v") {
                Vec3 p{};
                if (!(row >> p.x >> p.y >> p.z) ||
                    !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
                    std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) > 1e7 ||
                    positions.size() >= maxVertices * 10) return invalid();
                positions.push_back(p);
            } else if (type == "vt") {
                Vec2 uv{};
                if (!(row >> uv.x) || !std::isfinite(uv.x) || coordinates.size() >= maxVertices * 20) return invalid();
                if (row >> uv.y && !std::isfinite(uv.y)) return invalid();
                coordinates.push_back({uv.x, 1 - uv.y});
            } else if (type == "o" || type == "g") {
                finish();
                if (draft.size() >= 1000) return invalid();
                std::string name;
                std::getline(row, name);
                object.name = cleanName(std::move(name), draft.size() + 1);
            } else if (type == "f") {
                std::vector<std::size_t> indices;
                std::vector<Vec2> uv;
                std::string token;
                while (row >> token) {
                    if (indices.size() == 512) return invalid();
                    const std::string_view fields(token);
                    const auto slash = fields.find('/');
                    std::size_t vertex = 0;
                    if (!indexOf(fields.substr(0, slash), positions.size(), vertex)) return invalid();
                    const auto insertion = remap.emplace(vertex, object.mesh.vertices.size());
                    if (insertion.second) {
                        if (object.mesh.vertices.size() >= maxVertices) return invalid();
                        object.mesh.vertices.push_back(positions[vertex]);
                    }
                    indices.push_back(insertion.first->second);
                    Vec2 texel{};
                    if (slash != fields.npos && slash + 1 < fields.size()) {
                        const auto next = fields.find('/', slash + 1);
                        const auto value = fields.substr(slash + 1, next == fields.npos ? fields.npos : next - slash - 1);
                        if (!value.empty()) {
                            std::size_t tex = 0;
                            if (!indexOf(value, coordinates.size(), tex)) return invalid();
                            texel = coordinates[tex];
                        }
                    }
                    uv.push_back(texel);
                }
                if (indices.size() < 3 || object.mesh.faces.size() + indices.size() - 2 > maxFaces ||
                    !appendPolygon(object.mesh, indices, uv)) return invalid();
            }
        }
        finish();
        if (!input.eof() || draft.empty()) {
            error = L"The OBJ file contains no polygon mesh.";
            return false;
        }
        models = std::move(draft);
        return true;
    } catch (...) {
        error = L"Could not read this OBJ file.";
        return false;
    }
}

bool exportObj(const wchar_t* filename, const std::vector<Model>& models) {
    const std::filesystem::path path(filename);
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempFileNameW(path.parent_path().c_str(), L"OBJ", 0, temporary)) return false;
    bool saved = false;
    try {
        std::ofstream output(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
        output.imbue(std::locale::classic());
        output << "# 3D1 Editor\n" << std::setprecision(16);
        std::size_t firstVertex = 1, firstTexel = 1;
        for (int i = 0; i < int(models.size()); ++i) {
            const Model& model = models[i];
            if (!isGeometry(model.kind) || !objectVisible(models, i)) continue;
            output << "o " << cleanName(model.name, 1) << '\n';
            const Mesh& mesh = modelMesh(model);
            const auto& t = model.transform;
            for (Vec3 local : mesh.vertices) {
                const Vec3 p = t.position + t.basis[0] * (local.x * t.size[0]) +
                               t.basis[1] * (local.y * t.size[1]) + t.basis[2] * (local.z * t.size[2]);
                output << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
            }
            for (const auto& face : mesh.faces) {
                for (std::size_t i = 0; i < face.count; ++i) output << "vt " << face.uv[i].x << ' ' << 1 - face.uv[i].y << '\n';
                output << 'f';
                for (std::size_t i = 0; i < face.count; ++i) output << ' ' << firstVertex + face.vertices[i] << '/' << firstTexel++;
                output << '\n';
            }
            firstVertex += mesh.vertices.size();
        }
        output.close();
        saved = bool(output) && MoveFileExW(temporary, filename, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    } catch (...) {
        saved = false;
    }
    if (!saved) DeleteFileW(temporary);
    return saved;
}
