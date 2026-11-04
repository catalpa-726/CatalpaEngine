#include "catpch.h"
#include "Core/Serialization/JsonArchive.h"
#include <nlohmann/json.hpp>

namespace Catalpa {
    namespace {
        // SAX 在容器展开前检查深度，并在插入前拒绝重复键；不先分配无界 DOM。
        class ArchiveSax final : public nlohmann::json_sax<nlohmann::json> {
        public:
            ArchiveSax(const ArchiveLimits& limits, ArchiveError& error) : m_Limits(limits), m_Error(error) {}
            bool null() override { return Scalar(nullptr); }
            bool boolean(bool value) override { return Scalar(value); }
            bool number_integer(number_integer_t value) override { return Scalar(static_cast<int64_t>(value)); }
            bool number_unsigned(number_unsigned_t value) override { return Scalar(static_cast<uint64_t>(value)); }
            bool number_float(number_float_t value, const string_t&) override {
                return std::isfinite(value) ? Scalar(value) : Fail("non-finite number");
            }
            bool string(string_t& value) override {
                if (value.size() > m_Limits.maxStringBytes) return Fail("string size limit exceeded");
                return Scalar(std::move(value));
            }
            bool binary(binary_t&) override { return Fail("binary JSON is unsupported"); }
            bool start_object(size_t) override { return Start(true); }
            bool start_array(size_t) override { return Start(false); }
            bool key(string_t& value) override {
                if (m_Stack.empty()) return Fail("unexpected object key");
                auto& frame = m_Stack.back();
                if (value.size() > m_Limits.maxStringBytes) return Fail("key size limit exceeded");
                const auto& object = std::get<ArchiveValue::Object>(frame.value.data);
                if (object.contains(value)) {
                    m_Error = { ArchiveChildPath(frame.path, value), "duplicate object key" };
                    return false;
                }
                frame.key = std::move(value);
                return true;
            }
            bool end_object() override { return End(); }
            bool end_array() override { return End(); }
            bool parse_error(size_t position, const std::string&, const nlohmann::detail::exception& exception) override {
                return Fail("invalid JSON at byte " + std::to_string(position) + ": " + exception.what());
            }
            ArchiveValue TakeValue() { return std::move(m_Root); }

        private:
            struct Frame { ArchiveValue value; std::string path; std::string key; };
            std::string NextPath() const {
                if (m_Stack.empty()) return {};
                const auto& frame = m_Stack.back();
                if (const auto* array = std::get_if<ArchiveValue::Array>(&frame.value.data))
                    return ArchiveChildPath(frame.path, std::to_string(array->size()));
                return ArchiveChildPath(frame.path, frame.key);
            }
            bool Fail(std::string message) { m_Error = { NextPath(), std::move(message) }; return false; }
            bool CountNode() {
                if (m_Nodes >= m_Limits.maxNodes) return Fail("node limit exceeded");
                ++m_Nodes;
                return true;
            }
            template<class T> bool Scalar(T value) {
                if (!CountNode()) return false;
                ArchiveValue node; node.data = std::move(value);
                return Add(std::move(node));
            }
            bool Start(bool object) {
                if (m_Stack.size() >= m_Limits.maxDepth) return Fail("nesting depth limit exceeded");
                if (!CountNode()) return false;
                Frame frame; frame.path = NextPath();
                if (object) frame.value.data = ArchiveValue::Object{};
                else frame.value.data = ArchiveValue::Array{};
                m_Stack.push_back(std::move(frame));
                return true;
            }
            bool End() {
                if (m_Stack.empty()) return Fail("unexpected container end");
                ArchiveValue value = std::move(m_Stack.back().value);
                m_Stack.pop_back();
                return Add(std::move(value));
            }
            bool Add(ArchiveValue value) {
                if (m_Stack.empty()) { m_Root = std::move(value); return true; }
                auto& frame = m_Stack.back();
                if (auto* array = std::get_if<ArchiveValue::Array>(&frame.value.data)) {
                    if (array->size() >= m_Limits.maxCollectionEntries) return Fail("array size limit exceeded");
                    array->push_back(std::move(value));
                } else {
                    auto& object = std::get<ArchiveValue::Object>(frame.value.data);
                    if (object.size() >= m_Limits.maxCollectionEntries) return Fail("object size limit exceeded");
                    object.emplace(std::move(frame.key), std::move(value));
                }
                return true;
            }
            const ArchiveLimits& m_Limits;
            ArchiveError& m_Error;
            size_t m_Nodes = 0;
            ArchiveValue m_Root;
            std::vector<Frame> m_Stack;
        };

        // 输出流在 nlohmann 写入时执行硬字节界限，避免 dump 先生成过大临时字符串。
        class BoundedJsonBuffer final : public std::streambuf {
        public:
            explicit BoundedJsonBuffer(size_t maxBytes) : m_Limit(maxBytes) {}
            std::string TakeText() { return std::move(m_Text); }
        protected:
            std::streamsize xsputn(const char* text, std::streamsize count) override {
                if (count < 0 || static_cast<uint64_t>(count) > m_Limit - m_Text.size())
                    throw std::length_error("output byte limit exceeded");
                m_Text.append(text, static_cast<size_t>(count));
                return count;
            }
            int_type overflow(int_type value) override {
                if (traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
                const char text = traits_type::to_char_type(value);
                xsputn(&text, 1);
                return value;
            }
        private:
            size_t m_Limit;
            std::string m_Text;
        };

        // DOM 构造前累计最小字节占用，不能先拷贝大量字符串再在输出阶段拒绝。
        // 精确 UTF-8/转义和缩进输出仍由 nlohmann 写入上面的有界流。
        bool Encode(const ArchiveValue& value, nlohmann::json& output, const ArchiveLimits& limits,
            size_t depth, size_t& nodes, size_t& bytes, std::string_view path, ArchiveError& error) {
            auto fail = [&](std::string message) { error = { std::string(path), std::move(message) }; return false; };
            auto reserveBytes = [&](size_t amount) {
                if (amount > limits.maxBytes - bytes) return fail("output byte limit exceeded");
                bytes += amount;
                return true;
            };
            if (nodes >= limits.maxNodes) return fail("node limit exceeded");
            ++nodes;
            if (!reserveBytes(1)) return false;
            if (const auto* object = std::get_if<ArchiveValue::Object>(&value.data)) {
                if (depth >= limits.maxDepth || object->size() > limits.maxCollectionEntries)
                    return fail("object depth or size limit exceeded");
                output = nlohmann::json::object();
                for (const auto& [key, child] : *object) {
                    if (key.size() > limits.maxStringBytes) return fail("key size limit exceeded");
                    if (!reserveBytes(key.size())) return false;
                    if (!Encode(child, output[key], limits, depth + 1, nodes, bytes, ArchiveChildPath(path, key), error)) return false;
                }
            } else if (const auto* array = std::get_if<ArchiveValue::Array>(&value.data)) {
                if (depth >= limits.maxDepth || array->size() > limits.maxCollectionEntries)
                    return fail("array depth or size limit exceeded");
                output = nlohmann::json::array();
                for (size_t index = 0; index < array->size(); ++index) {
                    nlohmann::json child;
                    if (!Encode((*array)[index], child, limits, depth + 1, nodes, bytes,
                        ArchiveChildPath(path, std::to_string(index)), error)) return false;
                    output.push_back(std::move(child));
                }
            } else if (const auto* text = std::get_if<std::string>(&value.data)) {
                if (text->size() > limits.maxStringBytes) return fail("string size limit exceeded");
                if (!reserveBytes(text->size())) return false;
                output = *text;
            } else if (const auto* number = std::get_if<double>(&value.data)) {
                if (!std::isfinite(*number)) return fail("non-finite number");
                output = *number;
            } else if (const auto* number = std::get_if<int64_t>(&value.data)) output = *number;
            else if (const auto* number = std::get_if<uint64_t>(&value.data)) output = *number;
            else if (const auto* boolean = std::get_if<bool>(&value.data)) output = *boolean;
            else if (std::holds_alternative<std::nullptr_t>(value.data)) output = nullptr;
            else return fail("invalid archive value");
            return true;
        }
    }

    bool JsonArchive::Parse(std::string_view text, std::unique_ptr<ArchiveValue>& output, ArchiveError& error,
        const ArchiveLimits& limits) {
        error = {};
        if (text.size() > limits.maxBytes) { error.message = "input byte limit exceeded"; return false; }
        try {
            ArchiveSax sax(limits, error);
            if (!nlohmann::json::sax_parse(text.begin(), text.end(), &sax)) return false;
            auto candidate = std::make_unique<ArchiveValue>(sax.TakeValue());
            // variant切换到map值时可能分配；不能用值移动赋值覆盖调用方已发布的结果。
            static_assert(noexcept(output.swap(candidate)), "Archive publication must not partially fail");
            output.swap(candidate);
            return true;
        } catch (const std::exception& exception) {
            error.message = exception.what();
            return false;
        }
    }

    bool JsonArchive::Write(const ArchiveValue& value, std::string& output, ArchiveError& error,
        const ArchiveLimits& limits) {
        error = {};
        try {
            nlohmann::json json;
            size_t nodes = 0, bytes = 0;
            if (!Encode(value, json, limits, 0, nodes, bytes, {}, error)) return false;
            BoundedJsonBuffer buffer(limits.maxBytes);
            std::ostream stream(&buffer);
            stream.exceptions(std::ios::badbit | std::ios::failbit);
            stream.width(2);
            stream << json;
            stream.put('\n');
            std::string text = buffer.TakeText();
            output.swap(text);
            return true;
        } catch (const std::exception& exception) {
            error.message = exception.what();
            return false;
        }
    }
}
