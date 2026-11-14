#pragma once
#include "Core/Base.h"
#include "Core/Serialization/Archive.h"

namespace Catalpa {
	/* 组件模式描述持久字段，独立于 C++ 成员字节偏移。类型名称采用稳定标识。 */
	enum class ComponentFieldKind {
		Bool,
		Int,
		UInt,
		Number,
		String,
		Float2,
		Float3,
		Float4,
		StableId,
		StableIdArray,
		Object
	};
	struct ComponentFieldSchema {
		std::string id, displayName;
		ComponentFieldKind kind = ComponentFieldKind::Number;
		bool required = true;
		std::optional<double> minimum, maximum;
	};
	struct ComponentSchema {
		std::string typeId;
		uint32_t version = 1;
		std::vector<ComponentFieldSchema> fields;
		/* Runtime可补充TRS等领域语义，ECS基础模块不反向包含任何领域组件。 */
		std::function<bool(const ArchiveValue&, std::string&)> validate;
	};
	class CATALPA_API ComponentRegistry {
	public:
		/**
		 * @brief 初始化 ComponentRegistry。
		 */
		ComponentRegistry();
		~ComponentRegistry();
		ComponentRegistry(const ComponentRegistry&) = delete;
		ComponentRegistry& operator=(const ComponentRegistry&) = delete;
		/**
		 * @brief 移动构造 ComponentRegistry。
		 * @details 第 1 个参数（ComponentRegistry&&）为输入。移入其状态的源对象。
		 */
		ComponentRegistry(ComponentRegistry&&) noexcept;
		ComponentRegistry& operator=(ComponentRegistry&&) noexcept;
		[[nodiscard]] bool Register(ComponentSchema schema, std::string& error);
		/**
		 * @brief 校验整批组件模式并一次性发布新的注册索引。
		 * 先校验整批候选，再交换索引发布；重复或冲突注册保留原集合。
		 *
		 * @param[in] schemas 需要作为同一批次校验、发布的组件模式列表。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 所有模式均有效且无重复或冲突时为 true，否则保留原表并返回 false。
		 */
		[[nodiscard]] bool RegisterMany(std::span<const ComponentSchema> schemas, std::string& error);
		/**
		 * @brief 查找已注册的不可变组件模式。
		 * 返回指向已注册模式的借用指针，调用方只能读取其中的规则。
		 *
		 * @param[in] typeId 持久组件类型标识。
		 *
		 * @return 注册表持有的模式指针，未注册或不合法的类型返回 nullptr。
		 */
		[[nodiscard]] const ComponentSchema* Find(std::string_view typeId) const noexcept;
		[[nodiscard]] std::span<const std::shared_ptr<const ComponentSchema>> GetSchemas() const noexcept;
		/**
		 * @brief 按已注册的组件模式校验文档版本、字段及领域语义。
		 * 验证schemaVersion、字段类型/范围/未知字段，再调用已注册领域语义校验。
		 *
		 * @param[in] typeId 选择校验规则的组件类型标识。
		 * @param[in] value 待验证的组件归档对象，须包含匹配的模式版本以及该类型要求的字段。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 所有模式及领域检查通过时为 true，否则为 false。
		 */
		[[nodiscard]] bool Validate(std::string_view typeId, const ArchiveValue& value, std::string& error) const;

	private:
		struct Storage;
		std::unique_ptr<Storage> m_Storage;
	};
}
