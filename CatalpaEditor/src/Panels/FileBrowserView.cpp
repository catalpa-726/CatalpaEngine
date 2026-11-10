#include "catpch.h"
#include "Panels/FileBrowserView.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "imgui.h"

namespace Catalpa::Editor {
    namespace fs = std::filesystem;

    namespace {
        // 两端均为 canonical 路径。保留组件大小写，避免可区分大小写的同名目录混入作者根。
        bool ContainsDirectory(const fs::path& root, const fs::path& path) {
            const auto relative = path.lexically_relative(root);
            return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
        }
    }

    void FileBrowserView::Reset() {
        m_Thumbnails.Reset();
        m_Folders.Clear();
        m_Filter = {};
        m_Root.clear();
    }

    std::shared_ptr<const BrowserListing> BrowserDirectoryCache::List(const fs::path& root, const fs::path& requested) {
        if (root != m_Root) {
            Clear();
            m_Root = root;
        }
        if (const auto cached = m_Listings.find(requested); cached != m_Listings.end()) return cached->second;
        auto listing = std::make_shared<BrowserListing>();
        std::error_code ec;
        const auto path = fs::canonical(requested, ec);
        if (ec || !ContainsDirectory(root,path)) {
            listing->error = ec ? ec.message() : "Directory is outside the declared root";
            return listing;
        }
        if (const auto cached = m_Listings.find(path); cached != m_Listings.end()) return cached->second;
        fs::directory_iterator it(path, ec), end;
        size_t visited = 0;
        while (!ec && it != end && visited < 4096) {
            ++visited;
            std::error_code entryError;
            const auto actual = fs::canonical(it->path(), entryError);
            if (!entryError && ContainsDirectory(m_Root,actual)) {
                const bool folder = it->is_directory(entryError);
                // Only descend through direct children. A junction back to an ancestor must not produce a cycle.
                if (!entryError && (folder ? actual.parent_path() == path : it->is_regular_file(entryError))) {
                    listing->entries.push_back({actual, Tools::AssetPathText(it->path().filename()), folder});
                }
            }
            it.increment(ec);
        }
        listing->limited = it != end && !ec;
        if (ec) listing->error = ec.message();
        std::sort(listing->entries.begin(), listing->entries.end(), [](const BrowserEntry& a, const BrowserEntry& b) {
            return a.folder != b.folder ? a.folder > b.folder : a.name < b.name;
        });
        if (m_Listings.size() >= 96 || m_Count + listing->entries.size() > 32768) Clear();
        m_Count += listing->entries.size();
        m_Listings.emplace(path, listing);
        return listing;
    }

    void FileBrowserView::DrawFolder(const fs::path& path, std::string_view title, const fs::path& current,
                                     fs::path& navigate, unsigned depth, unsigned& budget) {
        if (!budget || depth > 48) return;
        --budget;
        const auto id = Tools::AssetPathText(path);
        auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (path == current) flags |= ImGuiTreeNodeFlags_Selected;
        if (ContainsDirectory(path,current)) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        const bool open = ImGui::TreeNodeEx(id.c_str(), flags, "%.*s", int(title.size()), title.data());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) navigate = path;
        if (!open) return;
        const auto listing = m_Folders.List(m_Root, path);
        for (const auto& child : listing->entries) {
            if (!child.folder || !budget) break;
            DrawFolder(child.path, child.name, current, navigate, depth + 1, budget);
        }
        ImGui::TreePop();
    }

    bool FileBrowserView::Draw(const fs::path& root, std::string_view rootLabel, fs::path& directory,
                               fs::path& selected, float height, const PanelText& text) {
        std::error_code ec;
        const auto canonicalRoot = fs::canonical(root, ec);
        if (ec) { ImGui::TextWrapped("%s", ec.message().c_str()); return false; }
        if (canonicalRoot != m_Root) {
            Reset();
            m_Root = canonicalRoot;
            directory = m_Root;
            selected.clear();
        }
        const auto oldSelection = selected;
        const auto oldDirectory = directory;
        fs::path navigate;
        if (ImGui::SmallButton(std::string(rootLabel).c_str())) navigate = m_Root;
        auto part = m_Root;
        for (const auto& item : directory.lexically_relative(m_Root)) {
            if (item == ".") continue;
            part /= item;
            ImGui::SameLine(); ImGui::TextDisabled("/"); ImGui::SameLine();
            const auto name = Tools::AssetPathText(item);
            const auto id = Tools::AssetPathText(part);
            ImGui::PushID(id.c_str());
            if (ImGui::SmallButton(name.c_str())) navigate = part;
            ImGui::PopID();
        }
        if (ImGui::Button(text("assets.refresh", "Refresh", "Refresh").c_str())) {
            m_Folders.Clear();
            m_Thumbnails.Reset();
        }
        ImGui::SameLine();
        if (ImGui::Button(text(m_Grid ? "browser.list" : "browser.grid", m_Grid ? "List" : "Grid", "Display").c_str()))
            m_Grid = !m_Grid;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputTextWithHint("##Filter", text("assets.filter","Filter",nullptr).c_str(), m_Filter.data(), m_Filter.size());

        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::BeginChild("Folders", {std::clamp(width * .26f, 65.f, 240.f), height}, ImGuiChildFlags_Borders);
        unsigned budget = 512;
        DrawFolder(m_Root, rootLabel, directory, navigate, 0, budget);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Files", {0,height}, ImGuiChildFlags_Borders);
        const auto listing = m_Folders.List(m_Root, directory);
        const auto tileWidth = ImGui::GetFontSize() * 5.f;
        const int cols = m_Grid ? std::max(1,int(ImGui::GetContentRegionAvail().x / tileWidth)) : 1;
        const float cellWidth = std::max(1.f,(ImGui::GetContentRegionAvail().x - (cols-1)*ImGui::GetStyle().ItemSpacing.x)/cols);
        int index = 0;
        for (const auto& entry : listing->entries) {
            if (entry.name.find(m_Filter.data()) == std::string::npos) continue;
            if (index++ % cols) ImGui::SameLine();
            const auto id = Tools::AssetPathText(entry.path);
            ImGui::PushID(id.c_str());
            const auto pos = ImGui::GetCursorScreenPos();
            const float textHeight = ImGui::GetTextLineHeightWithSpacing();
            const float thumbSize = std::min(cellWidth, ImGui::GetFontSize()*3.f);
            const ImVec2 size{cellWidth, m_Grid ? textHeight + thumbSize + 4 : textHeight};
            if (ImGui::Selectable("##File", selected == entry.path, ImGuiSelectableFlags_AllowDoubleClick, size)) {
                selected = entry.path;
                if (entry.folder && (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) ||
                                      ImGui::IsKeyPressed(ImGuiKey_Enter))) navigate = entry.path;
            }
            if (ImGui::IsItemVisible()) {
                auto* draw = ImGui::GetWindowDrawList();
                draw->PushClipRect(pos, {pos.x + size.x, pos.y + size.y}, true);
                std::string diagnostic;
                if (m_Grid) {
                    const ImVec2 a{pos.x + (size.x - thumbSize)*.5f, pos.y + 2}, b{a.x+thumbSize, a.y+thumbSize};
                    if (entry.folder) {
                        const float h = thumbSize*.19f;
                        draw->AddRectFilled({a.x,a.y+h},{a.x+thumbSize*.48f,a.y+2*h}, IM_COL32(86,157,125,255), 3);
                        draw->AddRectFilled({a.x,a.y+1.65f*h},{b.x,b.y-h}, IM_COL32(69,119,99,255), 3);
                    } else if (!m_Thumbnails.Draw(m_Root, entry.path, a,b,diagnostic)) {
                        draw->AddRect(a,b,ImGui::GetColorU32(ImGuiCol_Border),4.f);
                        const auto ext = Tools::AssetSourceExtension(entry.path);
                        draw->AddText({a.x+2,a.y+thumbSize*.3f}, ImGui::GetColorU32(ImGuiCol_TextDisabled), ext.c_str());
                    }
                }
                draw->AddText({pos.x+2, pos.y+(m_Grid ? thumbSize+3 : 0)}, ImGui::GetColorU32(ImGuiCol_Text),entry.name.c_str());
                draw->PopClipRect();
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(entry.name.c_str());
                    if (!diagnostic.empty()) ImGui::TextWrapped("%s",diagnostic.c_str());
                    ImGui::EndTooltip();
                }
            }
            ImGui::PopID();
        }
        if (listing->limited) ImGui::TextDisabled("%s", text("browser.limit","First 4096 entries",nullptr).c_str());
        if (!listing->error.empty()) ImGui::TextWrapped("%s",listing->error.c_str());
        ImGui::EndChild();

        if (!navigate.empty() && navigate != directory) {
            directory = std::move(navigate);
            selected.clear();
            m_Filter = {};
        }
        return directory != oldDirectory || selected != oldSelection;
    }
}
