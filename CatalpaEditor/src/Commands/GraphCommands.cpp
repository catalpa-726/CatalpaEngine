#include "Commands/GraphCommands.h"

namespace Catalpa::Editor {
    namespace {
        class SourceEdit final : public EditorCommand {
          public:
            SourceEdit(StableId id, std::shared_ptr<const std::string> before,
                       std::shared_ptr<const std::string> after, uint64_t group)
                : EditorCommand(id, "history.source"), m_Before(std::move(before)), m_After(std::move(after)), m_Group(group) {}
            bool TryMerge(const EditorCommand& command) noexcept override {
                const auto* next = dynamic_cast<const SourceEdit*>(&command);
                if (!next || !m_Group || m_Group != next->m_Group || Owner() != next->Owner() || m_After != next->m_Before) return false;
                m_After = next->m_After;
                return true;
            }
            bool Apply(Document& doc, std::string& error) override {
                auto* asset = dynamic_cast<AssetDocument*>(&doc);
                return asset && asset->ReplaceSource(m_Before, m_After, error);
            }
            bool Revert(Document& doc, std::string& error) override {
                auto* asset = dynamic_cast<AssetDocument*>(&doc);
                return asset && asset->ReplaceSource(m_After, m_Before, error);
            }
          private:
            std::shared_ptr<const std::string> m_Before, m_After;
            uint64_t m_Group;
        };
    }
    bool ChangeAuthorSource(AssetDocument& doc, std::string next, std::string& error, uint64_t group) {
        if (!doc.Source()) { error = "Document has no source draft"; return false; }
        if (*doc.Source() == next) return true;
        auto value = std::make_shared<const std::string>(std::move(next));
        return doc.History().Execute(doc, std::make_unique<SourceEdit>(doc.Id(), doc.Source(), value, group), error);
    }
}
