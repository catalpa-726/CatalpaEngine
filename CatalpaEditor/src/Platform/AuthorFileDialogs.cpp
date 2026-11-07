#include "Platform/AuthorFileDialogs.h"
#include <nfd.h>

namespace Catalpa::Editor {
    bool SelectAuthorFile(bool save, const std::filesystem::path& initialDirectory,
                          std::filesystem::path& selected, bool& cancelled, std::string& error,
                          std::string_view defaultExtension) {
        cancelled = false;
        if (NFD_Init() != NFD_OKAY) {
            error = NFD_GetError() ? NFD_GetError() : "Cannot initialize native file dialog";
            return false;
        }
        struct Owner {
            ~Owner() {
                NFD_Quit();
            }
        } owner;
        const auto utf = initialDirectory.u8string();
        const std::string initial(reinterpret_cast<const char*>(utf.data()), utf.size());
        nfdu8char_t* result = nullptr;
        const auto extensions =
            save && !defaultExtension.empty()
                ? std::string(defaultExtension)
                : std::string(
                      "scene,prefab,material,matgraph,behavior,lua,script,skeleton,animclip,animation,skin,"
                      "blendtree,particle,collision,physics-material,navmesh,terrain,ui,json");
        const nfdu8filteritem_t filters[]{{"Author documents", extensions.c_str()}};
        const auto status = save ? NFD_SaveDialogU8(&result, filters, 1, initial.c_str(), nullptr)
                                 : NFD_OpenDialogU8(&result, filters, 1, initial.c_str());
        if (status == NFD_CANCEL) {
            cancelled = true;
            return true;
        }
        if (status != NFD_OKAY) {
            error = NFD_GetError() ? NFD_GetError() : "Native file dialog failed";
            return false;
        }
        try {
            selected = std::filesystem::u8path(result);
        } catch (const std::exception& exception) {
            NFD_FreePathU8(result);
            error = exception.what();
            return false;
        }
        NFD_FreePathU8(result);
        return true;
    }
}
