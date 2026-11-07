#include "catpch.h"
#include "Physics/CollisionAssetLoader.h"
#include "Assets/AssetManager.h"
#include "Core/Serialization/AuthorJson.h"

namespace Catalpa {
    namespace {
        bool Finite(Math::Vec3 v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
        using AuthorJson::Json;
    } // namespace
    bool ValidateCollisionShape(const CollisionShapeAsset& value, std::string& error) {
        if (value.kind > CollisionShapeKind::TriangleMesh || !Finite(value.dimensions) ||
            value.dimensions.x < 0.001f || value.dimensions.y < 0.001f || value.dimensions.z < 0.001f ||
            std::max({value.dimensions.x, value.dimensions.y, value.dimensions.z}) > 10000 ||
            value.vertices.size() > 1000000 || value.indices.size() > 3000000) {
            error = "Collision shape dimensions/count exceed the metre-space budget";
            return false;
        }
        if (value.kind == CollisionShapeKind::ConvexHull &&
            (value.vertices.size() < 4 || value.vertices.size() > 256 || !value.indices.empty())) {
            error = "Convex hull requires 4..256 points and no triangle indices";
            return false;
        }
        if (value.kind == CollisionShapeKind::TriangleMesh &&
            (value.vertices.size() < 3 || value.indices.empty() || value.indices.size() % 3)) {
            error = "Triangle collider requires indexed triangles";
            return false;
        }
        if (value.kind < CollisionShapeKind::ConvexHull &&
            (!value.vertices.empty() || !value.indices.empty())) {
            error = "Primitive collider must not contain geometry arrays";
            return false;
        }
        for (auto v : value.vertices)
            if (!Finite(v) || std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)}) > 1e6f) {
                error = "Collider vertex is nonfinite or exceeds coordinate bounds";
                return false;
            }
        for (size_t i = 0; i < value.indices.size(); i += 3) {
            const auto a = value.indices[i], b = value.indices[i + 1], c = value.indices[i + 2];
            if (a >= value.vertices.size() || b >= value.vertices.size() || c >= value.vertices.size()) {
                error = "Collider triangle index is out of range";
                return false;
            }
            const auto ab = glm::dvec3(value.vertices[b]) - glm::dvec3(value.vertices[a]);
            const auto ac = glm::dvec3(value.vertices[c]) - glm::dvec3(value.vertices[a]);
            if (glm::dot(glm::cross(ab, ac), glm::cross(ab, ac)) == 0) {
                error = "Degenerate collider triangle";
                return false;
            }
        }
        error.clear();
        return true;
    }
    bool ValidatePhysicsMaterial(const PhysicsMaterialAsset& value, std::string& error) {
        if (!std::isfinite(value.friction) || value.friction < 0 || value.friction > 2 ||
            !std::isfinite(value.restitution) || value.restitution < 0 || value.restitution > 1) {
            error = "Friction must be [0,2] and restitution [0,1]";
            return false;
        }
        error.clear();
        return true;
    }
    bool ReadCollisionShape(std::string_view text, CollisionShapeAsset& output, std::string& error) try {
        std::unique_ptr<ArchiveValue> checked;
        ArchiveError archiveError;
        ArchiveLimits limits;
        limits.maxBytes = 64 * 1024 * 1024;
        limits.maxNodes = 8000000;
        limits.maxCollectionEntries = 3000000;
        if (!JsonArchive::Parse(text, checked, archiveError, limits))
            throw std::invalid_argument(archiveError.ToString());
        const auto doc = Json::parse(text);
        AuthorJson::Fields(doc, {"schemaVersion", "kind", "dimensions", "vertices", "indices"});
        if (AuthorJson::Unsigned(doc.at("schemaVersion")) != 1)
            throw std::invalid_argument("Unsupported collision shape schema");
        CollisionShapeAsset value;
        const auto kind = doc.at("kind").get<std::string>();
        const std::array names{"box", "sphere", "capsule", "convex_hull", "triangle_mesh"};
        const auto found = std::find(names.begin(), names.end(), kind);
        if (found == names.end())
            throw std::invalid_argument("Unknown collision shape kind");
        value.kind = static_cast<CollisionShapeKind>(found - names.begin());
        value.dimensions = AuthorJson::Vec3(doc.at("dimensions"));
        if (doc.contains("vertices")) {
            if (!doc["vertices"].is_array() || doc["vertices"].size() > 1000000)
                throw std::invalid_argument("Collider vertex budget");
            for (const auto& v : doc["vertices"])
                value.vertices.push_back(AuthorJson::Vec3(v));
        }
        if (doc.contains("indices")) {
            if (!doc["indices"].is_array() || doc["indices"].size() > 3000000)
                throw std::invalid_argument("Collider index budget");
            for (const auto& index : doc["indices"]) {
                const auto i = AuthorJson::Unsigned(index);
                if (i > UINT32_MAX)
                    throw std::invalid_argument("Collider index overflow");
                value.indices.push_back(uint32_t(i));
            }
        }
        if (!ValidateCollisionShape(value, error))
            return false;
        output = std::move(value);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool WriteCollisionShape(const CollisionShapeAsset& value, std::string& output, std::string& error) try {
        if (!ValidateCollisionShape(value, error))
            return false;
        const std::array names{"box", "sphere", "capsule", "convex_hull", "triangle_mesh"};
        Json doc{{"schemaVersion", 1},
                 {"kind", names[size_t(value.kind)]},
                 {"dimensions", AuthorJson::Array(value.dimensions)},
                 {"vertices", Json::array()},
                 {"indices", value.indices}};
        for (auto v : value.vertices)
            doc["vertices"].push_back(AuthorJson::Array(v));
        output = doc.dump();
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool ReadPhysicsMaterial(std::string_view text, PhysicsMaterialAsset& output, std::string& error) try {
        const auto doc = AuthorJson::Parse(text);
        AuthorJson::Fields(doc, {"schemaVersion", "friction", "restitution"});
        if (AuthorJson::Unsigned(doc.at("schemaVersion")) != 1)
            throw std::invalid_argument("Unsupported physics material schema");
        PhysicsMaterialAsset value{float(AuthorJson::Number(doc.at("friction"))),
                                   float(AuthorJson::Number(doc.at("restitution")))};
        if (!ValidatePhysicsMaterial(value, error))
            return false;
        output = value;
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool WritePhysicsMaterial(const PhysicsMaterialAsset& value, std::string& output,
                              std::string& error) try {
        if (!ValidatePhysicsMaterial(value, error))
            return false;
        output = Json{{"schemaVersion", 1}, {"friction", value.friction}, {"restitution", value.restitution}}
                     .dump();
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool RegisterPhysicsAssetLoaders(AssetManager& manager, std::string& error) {
        AssetLoader shape;
        if (!AssetType::TryParse("collision_shape", shape.type))
            return false;
        shape.cppType = typeid(CollisionShapeAsset);
        shape.decode = [](const AssetContainer& c, const AssetDecodeContext& context, AssetLoadResult& out,
                          std::string& error) {
            if (c.payloadVersion != 1 || !c.dependencies.empty() ||
                c.payload.size() > context.maxResidentBytes / 6 ||
                context.cancellation.IsCancellationRequested()) {
                error = "Collider decode version, dependency or budget failure";
                return false;
            }
            auto value = std::make_shared<CollisionShapeAsset>();
            if (!ReadCollisionShape({reinterpret_cast<const char*>(c.payload.data()), c.payload.size()},
                                    *value, error))
                return false;
            const auto bytes = sizeof(CollisionShapeAsset) + value->vertices.capacity() * sizeof(Math::Vec3) +
                               value->indices.capacity() * 4;
            out = MakeAssetLoadResult<CollisionShapeAsset>(value, bytes);
            return true;
        };
        if (!manager.RegisterLoader(std::move(shape), error))
            return false;
        AssetLoader material;
        if (!AssetType::TryParse("physics_material", material.type))
            return false;
        material.cppType = typeid(PhysicsMaterialAsset);
        material.decode = [](const AssetContainer& c, const AssetDecodeContext& context, AssetLoadResult& out,
                             std::string& error) {
            if (c.payloadVersion != 1 || !c.dependencies.empty() || c.payload.size() > 4096 ||
                context.cancellation.IsCancellationRequested()) {
                error = "Physics material decode rejected";
                return false;
            }
            auto value = std::make_shared<PhysicsMaterialAsset>();
            if (!ReadPhysicsMaterial({reinterpret_cast<const char*>(c.payload.data()), c.payload.size()},
                                     *value, error))
                return false;
            out = MakeAssetLoadResult<PhysicsMaterialAsset>(value, sizeof(PhysicsMaterialAsset));
            return true;
        };
        return manager.RegisterLoader(std::move(material), error);
    }
} // namespace Catalpa
