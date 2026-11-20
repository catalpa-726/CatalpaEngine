#include "Platform/AuthorFileDialogs.h"

#include <nfd.h>
#include "imgui_internal.h"
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#endif

namespace Catalpa::Editor {
	namespace {
		/**
		 * @brief 初始化原生选择器，并拒绝 NFD 默许的 UI 线程 MTA 回退。
		 * @param[out] error 初始化或线程模型不满足要求时接收可显示的原因。
		 * @return 初始化成功时为 true，调用方随后必须对称调用 NFD_Quit。
		 */
		bool InitializeDialog(std::string& error) {
			if (NFD_Init() != NFD_OKAY) {
				error = NFD_GetError() ? NFD_GetError() : "Cannot initialize native file dialog";
				return false;
			}
#if defined(_WIN32)
			APTTYPE apartment = APTTYPE_CURRENT;
			APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
			if (FAILED(CoGetApartmentType(&apartment, &qualifier)) ||
			    (apartment != APTTYPE_STA && apartment != APTTYPE_MAINSTA)) {
				error = "Native file dialogs require the Windows UI thread to use a single-threaded COM apartment";
				NFD_Quit();
				return false;
			}
#endif
			return true;
		}

		/**
		 * @brief 让系统选择器归属于发起操作的原生窗口，关闭后由 Windows 恢复其焦点和启用状态。
		 * @return 模态窗口优先，其次为当前面板或键盘焦点窗口；没有 ImGui 上下文时不指定所有者。
		 */
		nfdwindowhandle_t DialogParentWindow() {
			auto* context = ImGui::GetCurrentContext();
			if (!context)
				return {};
			auto* window = ImGui::GetTopMostPopupModal();
			if (!window)
				window = context->CurrentWindow;
			if (!window || window->IsFallbackWindow)
				window = context->NavWindow;
			auto* viewport = window && window->Viewport ? window->Viewport : ImGui::GetMainViewport();
			if (!viewport->PlatformHandleRaw)
				viewport = ImGui::GetMainViewport();
#if defined(_WIN32)
			return {NFD_WINDOW_HANDLE_TYPE_WINDOWS, viewport->PlatformHandleRaw};
#else
			return {};
#endif
		}
	}

	bool SelectAuthorFile(bool save, const std::filesystem::path& initialDirectory, std::filesystem::path& selected,
	    bool& cancelled, std::string& error, std::string_view defaultExtension, bool allFiles) {
		cancelled = false;
		if (!InitializeDialog(error)) {
			return false;
		}
		struct Owner {
			~Owner() { NFD_Quit(); }
		} owner;
		const auto utf = initialDirectory.u8string();
		const std::string initial(reinterpret_cast<const char*>(utf.data()), utf.size());
		nfdu8char_t* result = nullptr;
		const auto extensions = !defaultExtension.empty()
		    ? std::string(defaultExtension)
		    : std::string("scene,prefab,material,matgraph,behavior,cs,lua,script,skeleton,animclip,animation,skin,"
		                  "blendtree,particle,collision,physics-material,navmesh,terrain,ui,json");
		const nfdu8filteritem_t filters[]{{"Author documents", extensions.c_str()}};
		const auto parent = DialogParentWindow();
		const auto* defaultPath = initial.empty() ? nullptr : initial.c_str();
		nfdresult_t status;
		if (save) {
			nfdsavedialogu8args_t args{};
			args.filterList = allFiles ? nullptr : filters;
			args.filterCount = allFiles ? 0 : 1;
			args.defaultPath = defaultPath;
			args.parentWindow = parent;
			status = NFD_SaveDialogU8_With(&result, &args);
		} else {
			nfdopendialogu8args_t args{};
			args.filterList = allFiles ? nullptr : filters;
			args.filterCount = allFiles ? 0 : 1;
			args.defaultPath = defaultPath;
			args.parentWindow = parent;
			status = NFD_OpenDialogU8_With(&result, &args);
		}
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

	bool SelectAuthorFolder(const std::filesystem::path& initialDirectory, std::filesystem::path& selected,
	    bool& cancelled, std::string& error) {
		cancelled = false;
		if (!InitializeDialog(error)) {
			return false;
		}
		struct Owner {
			~Owner() { NFD_Quit(); }
		} owner;
		const auto utf = initialDirectory.u8string();
		const std::string initial(reinterpret_cast<const char*>(utf.data()), utf.size());
		nfdu8char_t* result = nullptr;
		nfdpickfolderu8args_t args{};
		args.defaultPath = initial.empty() ? nullptr : initial.c_str();
		args.parentWindow = DialogParentWindow();
		const auto status = NFD_PickFolderU8_With(&result, &args);
		if (status == NFD_CANCEL) {
			cancelled = true;
			return true;
		}
		if (status != NFD_OKAY) {
			error = NFD_GetError() ? NFD_GetError() : "Native folder dialog failed";
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
