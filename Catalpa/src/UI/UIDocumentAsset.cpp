#include "catpch.h"
#include "UI/UIDocumentAsset.h"
#include "Assets/AssetManager.h"
#include "Core/Serialization/BinaryArchive.h"

namespace Catalpa {
    namespace {
        template <class Map> size_t MapHeap(const Map& values) {
            size_t bytes = 4 * sizeof(void*);
            for (const auto& [key, value] : values) {
                bytes += sizeof(typename Map::value_type) + 4 * sizeof(void*) + key.capacity() + 1;
                if constexpr (std::is_same_v<typename Map::mapped_type, std::string>)
                    bytes += value.capacity() + 1;
            }
            return bytes;
        }
        size_t ResidentBytes(const UIDocumentAsset& value) {
            // 表格中即使只有短字符串，也存在独立节点；不能用序列化字节倍数代替值树容量。
            return sizeof(value) + value.entry.capacity() + 1 + MapHeap(value.files) +
                   MapHeap(value.imageIds) + MapHeap(value.fontIds) + MapHeap(value.values) +
                   MapHeap(value.images) + MapHeap(value.fonts);
        }
    } // namespace
    bool IsUIUtf8(std::string_view text) noexcept {
        for (size_t i = 0; i < text.size();) {
            const auto first = static_cast<unsigned char>(text[i++]);
            if (first < 0x80)
                continue;
            uint32_t scalar = 0, minimum = 0;
            unsigned count = 0;
            if (first >= 0xc2 && first <= 0xdf) {
                scalar = first & 31;
                minimum = 0x80;
                count = 1;
            } else if (first >= 0xe0 && first <= 0xef) {
                scalar = first & 15;
                minimum = 0x800;
                count = 2;
            } else if (first >= 0xf0 && first <= 0xf4) {
                scalar = first & 7;
                minimum = 0x10000;
                count = 3;
            } else
                return false;
            if (count > text.size() - i)
                return false;
            while (count--) {
                const auto next = static_cast<unsigned char>(text[i++]);
                if ((next & 0xc0) != 0x80)
                    return false;
                scalar = (scalar << 6) | (next & 63);
            }
            if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
                return false;
        }
        return true;
    }
    bool IsUIRelativePath(std::string_view path) noexcept {
        if (path.empty() || path.size() > 1024 || path.front() == '/' || path.back() == '/' ||
            !IsUIUtf8(path))
            return false;
        for (char c : path)
            if (c == '\\' || c == ':' || c == '?' || c == '#' || static_cast<unsigned char>(c) < 32)
                return false;
        size_t begin = 0;
        while (begin < path.size()) {
            const auto end = path.find('/', begin);
            const auto part = path.substr(begin, end == path.npos ? path.size() - begin : end - begin);
            if (part.empty() || part == "." || part == "..")
                return false;
            if (end == path.npos)
                break;
            begin = end + 1;
        }
        return true;
    }

    bool ValidateUIDocument(const UIDocumentAsset& asset, std::string& error) {
        if (!IsUIRelativePath(asset.entry) || !asset.files.contains(asset.entry) ||
            asset.files.size() > 128 || asset.imageIds.size() > 128 || asset.fontIds.size() > 16 ||
            asset.values.size() > 256) {
            error = "UI entry or resource count rejected";
            return false;
        }
        size_t bytes = 0;
        for (const auto& [path, text] : asset.files) {
            if (!IsUIRelativePath(path) || text.find('\0') != text.npos || text.size() > 2 * 1024 * 1024 ||
                !IsUIUtf8(text)) {
                error = "UI resource path/text rejected: " + path;
                return false;
            }
            bytes += text.size();
        }
        if (bytes > 8 * 1024 * 1024) {
            error = "UI document text budget exceeded";
            return false;
        }
        for (const auto* table : {&asset.imageIds, &asset.fontIds})
            for (const auto& [path, id] : *table)
                if (!IsUIRelativePath(path) || !id.HasValue() || asset.files.contains(path)) {
                    error = "UI image/font binding rejected: " + path;
                    return false;
                }
        for (const auto& [path, id] : asset.fontIds)
            if (asset.imageIds.contains(path)) {
                error = "UI image/font paths overlap: " + path;
                return false;
            }
        for (const auto& [id, value] : asset.values)
            if (id.empty() || id.size() > 256 || value.size() > 65536 || value.find('\0') != value.npos ||
                !IsUIUtf8(value) ||
                !((id.front() >= 'A' && id.front() <= 'Z') || (id.front() >= 'a' && id.front() <= 'z') ||
                  id.front() == '_') ||
                !std::all_of(id.begin(), id.end(), [](char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                           c == '_';
                })) {
                error = "UI value binding requires an identifier and bounded UTF-8 text";
                return false;
            }
        error.clear();
        return true;
    }

    bool UIDocumentLoader::Encode(const UIDocumentAsset& asset, std::vector<std::byte>& out,
                                  std::string& error) {
        if (!ValidateUIDocument(asset, error))
            return false;
        BinaryWriter writer(10 * 1024 * 1024);
        if (!writer.String(asset.entry) || !writer.U32(uint32_t(asset.files.size()))) {
            error = writer.Error();
            return false;
        }
        for (const auto& [path, text] : asset.files)
            if (!writer.String(path) || !writer.String(text)) {
                error = writer.Error();
                return false;
            }
        for (const auto* table : {&asset.imageIds, &asset.fontIds}) {
            if (!writer.U32(uint32_t(table->size()))) {
                error = writer.Error();
                return false;
            }
            for (const auto& [path, id] : *table)
                if (!writer.String(path) || !writer.String(id.ToString())) {
                    error = writer.Error();
                    return false;
                }
        }
        if (!writer.U32(uint32_t(asset.values.size()))) {
            error = writer.Error();
            return false;
        }
        for (const auto& [id, value] : asset.values)
            if (!writer.String(id) || !writer.String(value)) {
                error = writer.Error();
                return false;
            }
        out = writer.Data();
        error.clear();
        return true;
    }

    bool UIDocumentLoader::Register(AssetManager& manager, std::string& error) {
        AssetLoader loader;
        AssetType::TryParse("ui_document", loader.type);
        loader.cppType = typeid(UIDocumentAsset);
        loader.decode = [](const AssetContainer& container, const AssetDecodeContext& context,
                           AssetLoadResult& out, std::string& error) {
            if (container.payloadVersion != 1 || container.payload.size() > 10 * 1024 * 1024 ||
                container.payload.size() > context.maxResidentBytes / 3 ||
                context.cancellation.IsCancellationRequested()) {
                error = "UI payload version or resident budget rejected";
                return false;
            }
            UIDocumentAsset asset;
            BinaryReader reader(container.payload);
            uint32_t count = 0;
            if (!reader.String(asset.entry, 1024) || !reader.U32(count) || count > 128) {
                error = "UI file table rejected";
                return false;
            }
            for (uint32_t i = 0; i < count; ++i) {
                std::string path, text;
                if (!reader.String(path, 1024) || !reader.String(text, 2 * 1024 * 1024) ||
                    !asset.files.emplace(path, std::move(text)).second) {
                    error = "UI duplicate or malformed resource";
                    return false;
                }
            }
            std::set<AssetId> used;
            for (int kind = 0; kind < 2; ++kind) {
                if (!reader.U32(count) || count > (kind ? 16 : 128)) {
                    error = "UI resource table rejected";
                    return false;
                }
                for (uint32_t i = 0; i < count; ++i) {
                    std::string path, text;
                    AssetId id;
                    if (!reader.String(path, 1024) || !reader.String(text, 64) ||
                        !AssetId::TryParse(text, id)) {
                        error = "Invalid UI dependency identity";
                        return false;
                    }
                    const auto dep =
                        std::find_if(container.dependencies.begin(), container.dependencies.end(),
                                     [&](const auto& d) { return d.id == id; });
                    if (dep == container.dependencies.end()) {
                        error = "UI resource lacks a hard dependency";
                        return false;
                    }
                    if (kind == 0) {
                        auto value = context.FindDependency<ImageAsset>(id, dep->version);
                        AssetVersionRef<ImageAsset> ref;
                        if (!value ||
                            !AssetVersionRef<ImageAsset>::TryCreate(id, dep->version, value, ref, error) ||
                            !asset.imageIds.emplace(path, id).second) {
                            error = "UI image dependency rejected";
                            return false;
                        }
                        asset.images.emplace(path, std::move(ref));
                    } else {
                        auto value = context.FindDependency<FontAsset>(id, dep->version);
                        AssetVersionRef<FontAsset> ref;
                        if (!value ||
                            !AssetVersionRef<FontAsset>::TryCreate(id, dep->version, value, ref, error) ||
                            !asset.fontIds.emplace(path, id).second) {
                            error = "UI font dependency rejected";
                            return false;
                        }
                        asset.fonts.emplace(path, std::move(ref));
                    }
                    used.insert(id);
                }
            }
            if (used.size() != container.dependencies.size() || !reader.U32(count) || count > 256) {
                error = "UI dependency/value table mismatch";
                return false;
            }
            for (uint32_t i = 0; i < count; ++i) {
                std::string id, value;
                if (!reader.String(id, 256) || !reader.String(value, 65536) ||
                    !asset.values.emplace(id, std::move(value)).second) {
                    error = "UI value binding rejected";
                    return false;
                }
            }
            if (!reader.AtEnd() || !ValidateUIDocument(asset, error))
                return false;
            const size_t resident = ResidentBytes(asset);
            if (resident > context.maxResidentBytes) {
                error = "UI domain tree exceeds resident budget";
                return false;
            }
            out = MakeAssetLoadResult<UIDocumentAsset>(
                std::make_shared<const UIDocumentAsset>(std::move(asset)), resident);
            error.clear();
            return true;
        };
        return manager.RegisterLoader(std::move(loader), error);
    }
} // namespace Catalpa
