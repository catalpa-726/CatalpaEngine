#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "Assets/AssetManager.h"
#include "Documents/DocumentManager.h"

namespace Catalpa::Editor {
	/* 在文档切换和关闭时收束旧编辑器的独立 surface，再激活新类型。 */
	class AssetEditorRegistry {
	public:
		using Factory = std::unique_ptr<IAssetEditor> (*)();
		using OpenDocument = AssetDocument* (*)(AssetId, const AssetManager&, DocumentManager&, std::string&);
		/**
		 * @brief 初始化 AssetEditorRegistry。
		 */
		AssetEditorRegistry();
		/**
		 * @brief 为资源类型登记编辑器工厂和打开文档的回调。
		 *
		 * @param[in] type 用于匹配作者资产的类型键。
		 * @param[in] factory 创建该类型编辑器实例的工厂回调。
		 * @param[in] open 控制面板或窗口的打开状态。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Register(std::string_view type, Factory, OpenDocument, std::string&);
		/**
		 * @brief 按资源类型调用匹配的文档打开器。
		 *
		 * @param[in] id 用于定位目标的资产标识。
		 * @param[in] type 要查找对应编辑器的资产类型键。
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[in] docs 用于打开或复用目标资产文档的管理器。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 注册的打开器返回的文档指针；类型未注册或打开失败时为 nullptr。
		 */
		AssetDocument* Open(AssetId, std::string_view type, const AssetManager&, DocumentManager&, std::string&) const;
		/**
		 * @brief 按当前文档的类型选择对应编辑器并绘制其内容。
		 *
		 * @param[in] app 提供场景、资产和渲染服务的应用对象。
		 * @param[in,out] doc 当前编辑的文档。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时接收原因说明。
		 */
		void Draw(Application&, AssetDocument&, const PanelText&, std::string& error);
		/**
		 * @brief 关闭活动资源编辑器并释放保存的编辑状态。
		 *
		 * @param[in] app 提供场景、资产和渲染服务的应用对象。
		 */
		void Close(Application&);

	private:
		struct Entry {
			Factory factory;
			OpenDocument open;
		};

		std::map<std::string, Entry, std::less<>> m_Entries;
		std::unique_ptr<IAssetEditor> m_Active;
		/* 借用注册表内不会被替换的稳定键，不向文档借用字符串。 */
		std::string_view m_ActiveType;
	};
}
