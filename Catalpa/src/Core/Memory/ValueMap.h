#pragma once

#include <initializer_list>
#include <cstddef>
#include <map>
#include <memory>
#include <utility>

namespace Catalpa {
    // 独占的值映射：复制深拷贝，移动只转移指针。
    // MSVC 的 std::map 移动构造可能分配哨兵节点，不能直接用于要求无抛移动的 ECS 组件。
    template<class Key, class Value, class Compare = std::less<Key>>
    class ValueMap {
      public:
        using Storage = std::map<Key, Value, Compare>;
        using value_type = typename Storage::value_type;
        using iterator = typename Storage::iterator;
        using const_iterator = typename Storage::const_iterator;

        ValueMap() = default;
        ValueMap(std::initializer_list<value_type> values)
            : m_Value(std::make_unique<Storage>(values)) {}
        ValueMap(const Storage& values) : m_Value(std::make_unique<Storage>(values)) {}
        ValueMap(Storage&& values) : m_Value(std::make_unique<Storage>(std::move(values))) {}
        ValueMap(const ValueMap& other)
            : m_Value(other.m_Value ? std::make_unique<Storage>(*other.m_Value) : nullptr) {}
        ValueMap(ValueMap&&) noexcept = default;
        ValueMap& operator=(ValueMap&&) noexcept = default;

        ValueMap& operator=(const ValueMap& other) {
            if (this != &other) {
                ValueMap copy(other);
                swap(copy);
            }
            return *this;
        }

        // 兼容已有受检领域 API；可写访问只创建本对象独占的映射。
        operator const Storage&() const { return Read(); }
        operator Storage&() { return Write(); }

        bool empty() const noexcept { return !m_Value || m_Value->empty(); }
        std::size_t size() const noexcept { return m_Value ? m_Value->size() : 0; }
        void clear() noexcept { if (m_Value) m_Value->clear(); }
        void swap(ValueMap& other) noexcept { m_Value.swap(other.m_Value); }

        iterator begin() { return Write().begin(); }
        iterator end() { return Write().end(); }
        const_iterator begin() const { return Read().begin(); }
        const_iterator end() const { return Read().end(); }
        const_iterator cbegin() const { return Read().cbegin(); }
        const_iterator cend() const { return Read().cend(); }

        template<class K> iterator find(const K& key) { return Write().find(key); }
        template<class K> const_iterator find(const K& key) const { return Read().find(key); }
        template<class K> bool contains(const K& key) const { return m_Value && m_Value->contains(key); }
        Value& at(const Key& key) { return Write().at(key); }
        const Value& at(const Key& key) const { return Read().at(key); }
        Value& operator[](const Key& key) { return Write()[key]; }
        Value& operator[](Key&& key) { return Write()[std::move(key)]; }

        template<class... Args> auto emplace(Args&&... args) { return Write().emplace(std::forward<Args>(args)...); }
        template<class... Args> auto try_emplace(Args&&... args) { return Write().try_emplace(std::forward<Args>(args)...); }
        template<class... Args> auto insert_or_assign(Args&&... args) { return Write().insert_or_assign(std::forward<Args>(args)...); }
        template<class... Args> auto insert(Args&&... args) { return Write().insert(std::forward<Args>(args)...); }
        template<class... Args> auto erase(Args&&... args) { return Write().erase(std::forward<Args>(args)...); }

        bool operator==(const ValueMap& other) const { return Read() == other.Read(); }

      private:
        const Storage& Read() const {
            if (m_Value) return *m_Value;
            // 只读空值永不以非 const 引用暴露；移动后的对象仍可查询、复制或重新写入。
            static const Storage empty;
            return empty;
        }
        Storage& Write() {
            if (!m_Value) m_Value = std::make_unique<Storage>();
            return *m_Value;
        }

        std::unique_ptr<Storage> m_Value;
    };
}
