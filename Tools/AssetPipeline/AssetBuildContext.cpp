#include "catpch.h"
#include "AssetPipeline/AssetBuildContext.h"
#include "Core/PlatformServices.h"
#include <nlohmann/json.hpp>

namespace Catalpa::Tools {
    std::string AssetPathText(const std::filesystem::path& p) {
        const auto v = p.generic_u8string();
        return {reinterpret_cast<const char*>(v.data()), v.size()};
    }

    std::string AssetBuildDigest(std::span<const std::byte> b) {
        std::array<std::byte, 32> digest;
        std::string e;
        if (!ComputeSha256(b, digest, e))
            throw std::runtime_error(e);
        constexpr char h[] = "0123456789abcdef";
        std::string s;
        for (auto x : digest) {
            const auto v = std::to_integer<unsigned>(x);
            s += h[v >> 4];
            s += h[v & 15];
        }
        return s;
    }

    AssetId AssetBuildIdentity(std::string_view text) {
        std::array<std::byte, 32> d;
        std::string e;
        if (!ComputeSha256(std::as_bytes(std::span(text.data(), text.size())), d, e))
            throw std::runtime_error(e);
        uint64_t hi = 0, lo = 0;
        for (size_t i = 0; i < 8; ++i) {
            hi = (hi << 8) | std::to_integer<uint64_t>(d[i]);
            lo = (lo << 8) | std::to_integer<uint64_t>(d[i + 8]);
        }
        if (!(hi | lo))
            throw std::runtime_error("Zero derived identity");
        return AssetId::FromWords(hi, lo);
    }

    uint64_t AssetContentVersion(std::string_view hash) {
        if (hash.size() != 64 || !std::all_of(hash.begin(), hash.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            }))
            throw std::runtime_error("Invalid content digest");
        uint64_t v = 0;
        for (char c : hash.substr(0, 16))
            v = (v << 4) | uint64_t(c <= '9' ? c - '0' : c - 'a' + 10);
        return v ? v : 1;
    }

    std::string AssetImportSettingsKey(const AssetImportSettings& s) {
        return nlohmann::json{{"schema", 1},
                              {"unitScale", s.unitScale},
                              {"up", s.sourceUp},
                              {"normals", s.generateNormals},
                              {"tangents", s.generateTangents},
                              {"dependencyRemaps", s.dependencyRemaps},
                              {"maxSourceBytes", s.maxSourceBytes},
                              {"maxWorkingBytes", s.maxWorkingBytes},
                              {"maxVertices", s.maxVertices},
                              {"maxIndices", s.maxIndices},
                              {"maxNodes", s.maxNodes}}
            .dump();
    }

    bool AssetBuildContext::CheckCancelled(std::string& e) const {
        if (request.cancellation.IsCancellationRequested()) {
            e = "Asset build cancelled";
            return false;
        }
        return true;
    }

    bool AssetBuildContext::ReserveWorking(size_t bytes, std::string& e) {
        const auto limit = request.settings.maxWorkingBytes;
        if (m_ReadBytes > limit || m_WorkingBytes > limit - m_ReadBytes ||
            bytes > limit - m_ReadBytes - m_WorkingBytes) {
            e = "Imported payload/decode workspace exceeds configured working budget";
            return false;
        }
        m_WorkingBytes += bytes;
        return true;
    }

    bool AssetBuildContext::WorkspaceFits(size_t bytes, std::string& e) const {
        const auto limit = request.settings.maxWorkingBytes;
        if (m_ReadBytes > limit || m_WorkingBytes > limit - m_ReadBytes ||
            bytes > limit - m_ReadBytes - m_WorkingBytes) {
            e = "Transient image decode workspace exceeds configured working budget";
            return false;
        }
        return true;
    }

    bool AssetBuildContext::Initialize(std::string& e) try {
        if (!CheckCancelled(e))
            return false;
        if (!request.project.id.HasValue() || !request.source.is_absolute() ||
            !request.shaderRoot.is_absolute() || !request.project.cookedRoot.is_absolute() ||
            !request.project.derivedDataRoot.is_absolute()) {
            e = "Build requires explicit project identity and absolute source/shader/output roots";
            return false;
        }
        const auto& s = request.settings;
        if (s.dependencyRemaps.size() > 64) {
            e = "Dependency remap limit is 64";
            return false;
        }
        for (const auto& [uri, target] : s.dependencyRemaps)
            if (uri.empty() || target.empty() || uri.size() > 4096 || target.size() > 4096) {
                e = "Invalid dependency remap URI/target";
                return false;
            }
        if (!std::isfinite(s.unitScale) || s.unitScale <= 0 || s.unitScale > 1e9 ||
            (s.sourceUp != "auto" && s.sourceUp != "y" && s.sourceUp != "z") || s.maxSourceBytes == 0 ||
            s.maxWorkingBytes < s.maxSourceBytes || s.maxVertices == 0 || s.maxVertices > 2000000 ||
            s.maxIndices == 0 || s.maxIndices > 6000000 || s.maxNodes == 0 || s.maxNodes > 65536) {
            e = "Invalid bounded import settings";
            return false;
        }
        request.source = std::filesystem::canonical(request.source);
        request.shaderRoot = std::filesystem::canonical(request.shaderRoot);
        m_Roots.push_back(std::filesystem::canonical(request.project.contentRoot));
        m_RootNames.push_back("content");
        for (const auto& [name, p] : request.project.externalSourceRoots) {
            m_Roots.push_back(std::filesystem::canonical(p));
            m_RootNames.push_back("external/" + name);
        }
        request.project.cookedRoot = std::filesystem::weakly_canonical(request.project.cookedRoot);
        request.project.derivedDataRoot = std::filesystem::weakly_canonical(request.project.derivedDataRoot);
        for (const auto& out : {request.project.cookedRoot, request.project.derivedDataRoot})
            if (IsPathWithinRoot(out, request.shaderRoot) || IsPathWithinRoot(request.shaderRoot, out)) {
                e = "Derived roots overlap the read-only cooked shader input";
                return false;
            }
        if (IsPathWithinRoot(request.project.cookedRoot, request.project.derivedDataRoot) ||
            IsPathWithinRoot(request.project.derivedDataRoot, request.project.cookedRoot)) {
            e = "Cooked and DDC roots must be disjoint";
            return false;
        }
        for (const auto& p : m_Roots)
            for (const auto& out : {request.project.cookedRoot, request.project.derivedDataRoot})
                if (IsPathWithinRoot(out, p) || IsPathWithinRoot(p, out)) {
                    e = "Derived roots overlap a read-only source root";
                    return false;
                }
        bool allowed = false;
        for (size_t i = 0; i < m_Roots.size(); ++i)
            if (IsPathWithinRoot(request.source, m_Roots[i])) {
                m_SourceKey =
                    m_RootNames[i] + "/" + AssetPathText(request.source.lexically_relative(m_Roots[i]));
                allowed = true;
                break;
            }
        if (!allowed) {
            e = "Source is outside the project's declared read-only roots";
            return false;
        }
        e.clear();
        return true;
    } catch (const std::exception& exception) {

        e = exception.what();
        return false;
    }

    AssetId AssetBuildContext::Identity(std::string_view key) const {
        return AssetBuildIdentity(request.project.id.ToString() + "/" + m_SourceKey + "/" + std::string(key));
    }

    void AssetBuildContext::Diagnose(AssetDiagnosticLevel level, std::string element, std::string message) {
        if (diagnostics.size() < 1024)
            diagnostics.push_back(
                {level, AssetPathText(request.source), std::move(element), std::move(message)});
        else {
            m_OmittedDiagnostics += m_OmittedDiagnostics ? 1 : 2;
            diagnostics.back() = {AssetDiagnosticLevel::Warning, AssetPathText(request.source),
                                  "diagnostics-limit",
                                  std::to_string(m_OmittedDiagnostics) +
                                      " import diagnostics omitted by the 1024-record bound"};
        }
    }

    bool AssetBuildContext::ReadSource(const std::filesystem::path& path, size_t byteLimit,
                                       std::vector<std::byte>& bytes, std::string& error) const {
        for (const auto& root : m_Roots) {
            if (IsPathWithinRoot(path, root)) {
                VirtualFileSystem files;
                VirtualPath source;
                return files.MountReadOnly("source", root, 0, error) &&
                       VirtualPath::Parse("source:/" + AssetPathText(path.lexically_relative(root)), source,
                                          error) &&
                       files.ReadFile(source, byteLimit, bytes, error);
            }
        }
        error = "Read escaped declared source roots: " + AssetPathText(path);
        return false;
    }

    bool AssetBuildContext::Read(const std::filesystem::path& input, std::vector<std::byte>& out,
                                 std::string& e) try {
        if (!CheckCancelled(e))
            return false;
        if (!input.is_absolute()) {
            e = "Source reads require a resolved absolute path";
            return false;
        }
        const auto path = std::filesystem::canonical(input);
        const auto previous = std::find_if(m_Dependencies.begin(), m_Dependencies.end(),
                                           [&](const auto& item) { return item.path == path; });
        size_t limit = request.settings.maxSourceBytes;
        if (previous == m_Dependencies.end()) {
            if (m_Dependencies.size() >= 4096) {
                e = "Source batch exceeds 4096 dependencies";
                return false;
            }
            limit = std::min(limit, request.settings.maxWorkingBytes - m_ReadBytes - m_WorkingBytes);
        }
        std::vector<std::byte> bytes;
        if (!ReadSource(path, limit, bytes, e))
            return false;
        const auto hash = AssetBuildDigest(bytes);
        if (previous == m_Dependencies.end()) {
            m_Dependencies.push_back({path, hash, bytes.size()});
            m_ReadBytes += bytes.size();
        } else if (previous->sha256 != hash) {
            e = "Source changed during import: " + AssetPathText(path);
            return false;
        }
        out.swap(bytes);
        return true;
    } catch (const std::exception& exception) {

        e = exception.what();
        return false;
    }

    bool AssetBuildContext::Resolve(const std::filesystem::path& owner, std::string_view reference,
                                    std::filesystem::path& out, std::string& e,
                                    SourceReferenceSyntax syntax) try {
        const auto mapped = request.settings.dependencyRemaps.find(reference);
        if (mapped != request.settings.dependencyRemaps.end()) {
            Diagnose(AssetDiagnosticLevel::Warning, std::string(reference),
                     "Explicit dependency remap to " + mapped->second);
            reference = mapped->second;
            syntax = SourceReferenceSyntax::FilePath;
        }
        std::string decoded;
        decoded.reserve(reference.size());
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i < reference.size(); ++i) {
            char c = reference[i];
            if (syntax == SourceReferenceSyntax::Uri && (c == '?' || c == '#')) {
                e = "Query and fragment are unsupported in external asset URIs";
                return false;
            }
            if (syntax == SourceReferenceSyntax::Uri && c == '%') {
                if (i + 2 >= reference.size() || hex(reference[i + 1]) < 0 || hex(reference[i + 2]) < 0) {
                    e = "Invalid percent-encoded source URI";
                    return false;
                }
                c = char(hex(reference[i + 1]) * 16 + hex(reference[i + 2]));
                i += 2;
            }
            if (!c) {
                e = "Source reference contains a NUL";
                return false;
            }
            decoded += c == '\\' ? '/' : c;
        }
        if (decoded.empty() || decoded.find("://") != std::string::npos || decoded.starts_with("data:")) {
            e = "External dependency requires a local file URI";
            return false;
        }
        const auto relative = std::filesystem::u8path(decoded);
        if (!relative.is_absolute() && (relative.has_root_path() || !owner.is_absolute())) {
            e = "Relative dependencies require an absolute owner and no drive-relative prefix";
            return false;
        }
        const auto p =
            std::filesystem::canonical(relative.is_absolute() ? relative : owner.parent_path() / relative);
        for (const auto& root : m_Roots)
            if (IsPathWithinRoot(p, root)) {
                out = p;
                return true;
            }
        e = "Dependency escapes declared source roots: " + decoded;
        return false;
    } catch (const std::exception& exception) {

        e = exception.what();
        return false;
    }

    bool AssetBuildContext::InputsUnchanged(std::string& e) const try {
        for (const auto& d : m_Dependencies) {
            if (!CheckCancelled(e))
                return false;
            std::vector<std::byte> b;
            if (!ReadSource(d.path, request.settings.maxSourceBytes, b, e))
                return false;
            if (AssetBuildDigest(b) != d.sha256) {
                e = "Input changed before publication: " + AssetPathText(d.path);
                return false;
            }
        }
        return true;
    } catch (const std::exception& exception) {

        e = exception.what();
        return false;
    }
}
