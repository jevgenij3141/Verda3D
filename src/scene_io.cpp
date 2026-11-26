#include "scene_io.h"
#include "material_library.h"
#include "scene_mesh.h"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
constexpr char magic[] = "3D1SCENE";
constexpr unsigned version = 11;
constexpr std::uintmax_t maxFileBytes = 512ull * 1024 * 1024;

template<bool reading> struct Archive {
    using Stream = std::conditional_t<reading, std::ifstream, std::ofstream>;
    Stream& stream;
    unsigned fileVersion = version;

    bool bytes(void* data, std::size_t size) {
        if (!size) return true;
        if constexpr (reading) stream.read(static_cast<char*>(data), size);
        else stream.write(static_cast<const char*>(data), size);
        return bool(stream);
    }

    template<class T> bool scalar(T& value) {
        if (!bytes(&value, sizeof(value))) return false;
        if constexpr (std::is_floating_point_v<T>) return std::isfinite(value);
        return true;
    }

    template<class E> bool enumeration(E& value, int max) {
        int index = int(value);
        if (!scalar(index) || index < 0 || index > max) return false;
        if constexpr (reading) value = static_cast<E>(index);
        return true;
    }

    bool flag(bool& value) {
        unsigned char state = value ? 1 : 0;
        if (!scalar(state) || state > 1) return false;
        if constexpr (reading) value = state != 0;
        return true;
    }

    template<class Container> bool resize(Container& data, std::size_t max) {
        if (!reading && data.size() > max) return false;
        unsigned size = static_cast<unsigned>(data.size());
        if (!scalar(size) || size > max) return false;
        if constexpr (reading) data.resize(size);
        return true;
    }

    template<class Container> bool blob(Container& value, std::size_t max) {
        return resize(value, max) && bytes(value.data(), value.size() * sizeof(typename Container::value_type));
    }

    bool vector(Vec3& v) {
        return scalar(v.x) && scalar(v.y) && scalar(v.z) &&
               std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)}) <= 1e8;
    }
    bool color(Color& c) {
        return scalar(c.r) && scalar(c.g) && scalar(c.b) &&
               std::min({c.r, c.g, c.b}) >= 0 && std::max({c.r, c.g, c.b}) <= 1;
    }

    bool mesh(Mesh& mesh, ObjectKind kind) {
        if (!resize(mesh.vertices, 100000) || !resize(mesh.faces, 100000)) return false;
        if (mesh.faces.empty() != mesh.vertices.empty()) return false;
        if ((!isGeometry(kind) && !mesh.faces.empty()) ||
            (isGeometry(kind) && kind != ObjectKind::Box && mesh.faces.empty())) return false;
        for (Vec3& p : mesh.vertices) {
            if (!vector(p)) return false;
        }
        for (MeshFace& face : mesh.faces) {
            unsigned count = static_cast<unsigned>(face.count);
            if (!scalar(count) || (count != 3 && count != 4)) return false;
            if constexpr (reading) face.count = count;
            for (std::size_t i = 0; i < face.count; ++i) {
                unsigned index = static_cast<unsigned>(face.vertices[i]);
                if (!scalar(index) || index >= mesh.vertices.size() ||
                    !scalar(face.uv[i].x) || !scalar(face.uv[i].y)) return false;
                if constexpr (reading) face.vertices[i] = index;
            }
            const Vec3 origin = mesh.vertices[face.vertices[0]];
            const Vec3 n = cross(mesh.vertices[face.vertices[1]] - origin,
                                 mesh.vertices[face.vertices[face.count - 1]] - origin);
            if (dot(n, n) <= 1e-20) return false;
        }
        return true;
    }

    bool transformAxes(Transform& transform) {
        for (auto& axis : transform.basis) {
            if (!vector(axis) || std::abs(dot(axis, axis) - 1) > .001) return false;
        }
        const auto& basis = transform.basis;
        if (dot(cross(basis[0], basis[1]), basis[2]) < .999) return false;
        for (auto& size : transform.size) {
            if (!scalar(size) || size < .05 || size > 100) return false;
        }
        return true;
    }

    bool model(Model& model) {
        if (!enumeration(model.kind, fileVersion < 7 ? int(ObjectKind::Mesh) : int(ObjectKind::Group)) ||
            !blob(model.name, 128) || model.name.empty() ||
            !scalar(model.material) || !vector(model.transform.position)) return false;
        if (fileVersion >= 2 && !flag(model.hidden)) return false;
        if (!transformAxes(model.transform)) return false;
        if (isLight(model.kind)) {
            Light& l = model.light;
            if (!color(l.color) || !scalar(l.power) || l.power < 0 || l.power > 10000 ||
                !scalar(l.range) || l.range < 0 || l.range > 500 || !scalar(l.radius) || l.radius < 0 || l.radius > 25 ||
                !scalar(l.cone) || l.cone < 1 || l.cone > 160 || !flag(l.enabled) || !flag(l.shadows) ||
                !scalar(l.shadowSize) || l.shadowSize < minShadowSize || l.shadowSize > maxShadowSize ||
                !scalar(l.shadowTransparency) || l.shadowTransparency < 0 || l.shadowTransparency > 1) return false;
        }
        if (!mesh(model.mesh, model.kind)) return false;
        if (fileVersion < 3) return true;
        if (!resize(model.keys, maxTimelineFrames)) return false;
        int previous = -1;
        for (auto& key : model.keys) {
            if (!scalar(key.frame) || key.frame <= previous || key.frame >= maxTimelineFrames ||
                !vector(key.pose.position) || !transformAxes(key.pose)) return false;
            if (fileVersion >= 4 && !enumeration(key.interpolation, int(KeyInterpolation::Smooth))) return false;
            previous = key.frame;
        }
        if (fileVersion < 6) return true;
        if (!resize(model.modifiers, maxModifiers) || (!isGeometry(model.kind) && !model.modifiers.empty())) return false;
        for (auto& modifier : model.modifiers) {
            if (!flag(modifier.active) || !scalar(modifier.levels) ||
                modifier.levels < 1 || modifier.levels > maxSmoothLevels) return false;
        }
        if (fileVersion >= 7 && (!scalar(model.parent) || !flag(model.expanded))) return false;
        if (fileVersion < 10) return true;
        if (model.kind == ObjectKind::Plane &&
            (!scalar(model.planeSegmentsX) || !scalar(model.planeSegmentsY) ||
             model.planeSegmentsX < 1 || model.planeSegmentsX > maxPlaneSegments ||
             model.planeSegmentsY < 1 || model.planeSegmentsY > maxPlaneSegments)) return false;
        if (!enumeration(model.meshShading, int(MeshShading::Flat)) ||
            (!isGeometry(model.kind) && model.meshShading != MeshShading::Default)) return false;
        if (fileVersion < 11 || !isGeometry(model.kind)) return true;
        auto& defaults = model.defaultTextureUvs;
        if (!resize(defaults, 100000) || (!defaults.empty() && defaults.size() != model.mesh.faces.size())) return false;
        for (std::size_t i = 0; i < defaults.size(); ++i)
            for (std::size_t j = 0; j < model.mesh.faces[i].count; ++j)
                if (!scalar(defaults[i][j].x) || !scalar(defaults[i][j].y)) return false;
        return true;
    }

    bool material(MaterialEdit& edit) {
        Surface& s = edit.surface;
        if (!color(s.tint) || !enumeration(s.shading, int(Shading::Unlit)) ||
            !enumeration(edit.preset, int(presetCount))) return false;
        for (int* value : {&s.ambient, &s.diffuse, &s.specular, &s.emission}) {
            if (!scalar(*value) || *value < 0 || *value > 100) return false;
        }
        auto& image = edit.image;
        if (!scalar(s.shininess) || s.shininess < 1 || s.shininess > 128 ||
            !scalar(image.width) || !scalar(image.height) ||
            image.width < 0 || image.width > 1024 || image.height < 0 || image.height > 1024 ||
            !blob(image.filename, 32768)) return false;
        const auto dataSize = std::size_t(image.width) * image.height * 4;
        if (!reading && image.pixels.size() != dataSize) return false;
        if constexpr (reading) image.pixels.resize(dataSize);
        if (!bytes(image.pixels.data(), dataSize)) return false;
        if (fileVersion >= 11 && (!scalar(edit.repeatX) || !scalar(edit.repeatY) ||
            edit.repeatX < 1 || edit.repeatY < 1 || edit.repeatX > maxTextureRepeat || edit.repeatY > maxTextureRepeat))
            return false;
        if (fileVersion < 5) return true;
        bool customOptics = edit.optics.has_value();
        if (!flag(customOptics)) return false;
        if (!customOptics) return true;
        if constexpr (reading) edit.optics.emplace();
        Optics& optics = *edit.optics;
        return scalar(optics.transmission) && optics.transmission >= 0 && optics.transmission <= 1 &&
               scalar(optics.index) && optics.index >= 1 && optics.index <= 2.5 &&
               scalar(optics.causticStrength) && optics.causticStrength >= 0 && optics.causticStrength <= 4;
    }

    bool document(SceneDocument& scene) {
        char token[sizeof(magic)]{};
        if constexpr (!reading) std::memcpy(token, magic, sizeof(magic));
        if (!bytes(token, sizeof(token)) || std::memcmp(token, magic, sizeof(token)) != 0 ||
            !scalar(fileVersion) || fileVersion < 1 || fileVersion > version ||
            !resize(scene.models, 10000) || !resize(scene.materials, 256)) return false;
        for (Model& item : scene.models) {
            if (!model(item) || item.material < -1 || item.material >= int(scene.materials.size()) ||
                (!isGeometry(item.kind) && item.material != -1)) return false;
        }
        if (fileVersion >= 7) {
            std::vector<unsigned char> checked(scene.models.size());
            for (const auto& item : scene.models) {
                if (item.parent < -1 || item.parent >= int(scene.models.size()) ||
                    (item.parent >= 0 && scene.models[item.parent].kind != ObjectKind::Group)) return false;
            }
            for (int i = 0; i < int(scene.models.size()); ++i) {
                int node = i;
                while (node >= 0 && checked[node] == 0) {
                    checked[node] = 1;
                    node = scene.models[node].parent;
                }
                if (node >= 0 && checked[node] == 1) return false;
                node = i;
                while (node >= 0 && checked[node] == 1) {
                    checked[node] = 2;
                    node = scene.models[node].parent;
                }
            }
        }
        for (MaterialEdit& item : scene.materials) {
            if (!material(item)) return false;
        }
        for (SavedView& view : scene.views) {
            if (!vector(view.back) || !vector(view.right) || !vector(view.up) || !vector(view.target) ||
                std::abs(dot(view.back, view.back) - 1) > .001 || std::abs(dot(view.right, view.right) - 1) > .001 ||
                std::abs(dot(view.up, view.up) - 1) > .001 || !scalar(view.zoom) || view.zoom < minViewZoom || view.zoom > 8 ||
                !scalar(view.perspective) || (view.perspective != 0 && view.perspective != 1) ||
                !scalar(view.orbited) || (view.orbited != 0 && view.orbited != 1) ||
                !scalar(view.camera) || view.camera < -1 || view.camera >= int(scene.models.size()) ||
                (view.camera >= 0 && scene.models[view.camera].kind != ObjectKind::Camera)) return false;
        }
        const bool valid = enumeration(scene.rendering.mode, 1) && scalar(scene.rendering.samples) &&
               scene.rendering.samples >= 1 && scene.rendering.samples <= maxRenderSamples &&
               scalar(scene.activeView) && scene.activeView >= 0 && scene.activeView < int(scene.views.size()) &&
               scalar(scene.selected) && scene.selected >= -1 && scene.selected < int(scene.models.size());
        if (!valid || fileVersion < 3) return valid;
        auto& timeline = scene.timeline;
        const bool validTimeline = scalar(timeline.fps) && timeline.fps >= 1 && timeline.fps <= maxTimelineFps &&
               scalar(timeline.frames) && timeline.frames >= 1 && timeline.frames <= maxTimelineFrames &&
               scalar(timeline.frame) && timeline.frame >= 0 && timeline.frame < timeline.frames;
        if (!validTimeline || fileVersion < 5) return validTimeline;
        const bool validCaustics = flag(scene.rendering.caustics) && scalar(scene.rendering.causticPhotons) &&
               scene.rendering.causticPhotons >= minCausticPhotons && scene.rendering.causticPhotons <= maxCausticPhotons;
        if (!validCaustics || fileVersion < 8) return validCaustics;
        auto& rendering = scene.rendering;
        if (!scalar(rendering.width) || rendering.width < minRenderDimension || rendering.width > maxRenderDimension ||
            !scalar(rendering.height) || rendering.height < minRenderDimension || rendering.height > maxRenderDimension) return false;
        for (auto& view : scene.views)
            if (!flag(view.showRenderFrame)) return false;
        if (fileVersion >= 9)
            for (auto& view : scene.views)
                if (!enumeration(view.mode, int(ViewportMode::Software))) return false;
        return true;
    }
};
} // namespace

bool writeScene(const wchar_t* filename, SceneDocument& scene) {
    const std::filesystem::path path(filename);
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempFileNameW(path.parent_path().c_str(), L"3D1", 0, temporary)) return false;
    bool written = false;
    try {
        std::ofstream file(std::filesystem::path{temporary}, std::ios::binary | std::ios::trunc);
        Archive<false> archive{file};
        written = archive.document(scene) && file.tellp() <= std::streamoff(maxFileBytes);
        file.close();
        written = written && bool(file);
    } catch (...) {
        written = false;
    }
    if (written) written = MoveFileExW(temporary, filename, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!written) DeleteFileW(temporary);
    return written;
}

bool readScene(const wchar_t* filename, SceneDocument& scene) {
    try {
        const std::filesystem::path path(filename);
        if (std::filesystem::file_size(path) > maxFileBytes) return false;
        std::ifstream file(path, std::ios::binary);
        SceneDocument draft;
        Archive<true> archive{file};
        if (!archive.document(draft) || file.peek() != std::ifstream::traits_type::eof()) return false;
        for (Model& model : draft.models) {
            if (archive.fileVersion < 10 && model.kind == ObjectKind::Plane && model.mesh.faces.size() > 1)
                model.kind = ObjectKind::Mesh;
            if (!rebuildModifiers(model)) return false;
        }
        scene = std::move(draft);
        return true;
    } catch (...) {
        return false;
    }
}
