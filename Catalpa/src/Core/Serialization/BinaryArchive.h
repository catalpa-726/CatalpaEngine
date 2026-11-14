#pragma once
#include "catpch.h"

#include "Core/Base.h"
namespace Catalpa {
	/* 各标量按明确的小端序协议编码，不写入原生结构体的填充字节或指针。 */
	class CATALPA_API BinaryWriter {
	public:
		explicit BinaryWriter(size_t maxBytes = 64 * 1024 * 1024) : m_Limit(maxBytes) {}
		bool U8(uint8_t);
		/**
		 * @brief 写入32 位无符号整数。
		 *
		 * @param[in] value 待按小端序追加到输出归档的无符号整数。
		 *
		 * @return 成功追加 4 字节时为 true，超出预算或写入失败时为 false。
		 */
		bool U32(uint32_t);
		/**
		 * @brief 写入64 位无符号整数。
		 *
		 * @param[in] value 待按小端序追加到输出归档的无符号整数。
		 *
		 * @return 成功追加 8 字节时为 true，超出预算或写入失败时为 false。
		 */
		bool U64(uint64_t);
		/**
		 * @brief 写入32 位浮点数。
		 *
		 * @param[in] value 以 IEEE 754 单精度位模式按小端序写入的数值。
		 *
		 * @return 成功写入单精度数的 4 个字节时为 true，否则为 false。
		 */
		bool F32(float);
		/**
		 * @brief 在容量限制内将字节追加到二进制归档。
		 *
		 * @param[in] bytes 按原顺序追加到输出归档的字节。
		 *
		 * @return 字节全部追加成功时为 true，超出容量或写入失败时为 false。
		 */
		bool Bytes(std::span<const std::byte>);
		/**
		 * @brief 以 32 位长度前缀写出字符串的原始字节。
		 *
		 * @param[in] value 采用 32 位长度前缀写入的字符串字节。
		 *
		 * @return 长度前缀与字符串字节均写入成功时为 true，否则为 false。
		 */
		bool String(std::string_view);
		[[nodiscard]] const std::vector<std::byte>& Data() const noexcept { return m_Data; }
		[[nodiscard]] const std::string& Error() const noexcept { return m_Error; }

	private:
		size_t m_Limit;
		std::vector<std::byte> m_Data;
		std::string m_Error;
	};
	class CATALPA_API BinaryReader {
	public:
		explicit BinaryReader(std::span<const std::byte> bytes) : m_Data(bytes) {}
		/**
		 * @brief 读取8 位无符号整数。
		 *
		 * @param[out] output 接收 uint8_t 结果。
		 *
		 * @return 读出下一个字节时为 true，输入不足或此前读取失败时为 false。
		 */
		bool U8(uint8_t&);
		/**
		 * @brief 读取32 位无符号整数。
		 *
		 * @param[out] output 接收32 位无符号整数结果。
		 *
		 * @return 完整读取 4 字节并解码整数时为 true，否则为 false。
		 */
		bool U32(uint32_t&);
		/**
		 * @brief 读取64 位无符号整数。
		 *
		 * @param[out] output 接收64 位无符号整数结果。
		 *
		 * @return 完整读取 8 字节并解码整数时为 true，否则为 false。
		 */
		bool U64(uint64_t&);
		/**
		 * @brief 读取32 位浮点数。
		 *
		 * @param[out] output 接收由小端 32 位位模式恢复的单精度浮点数。
		 *
		 * @return 完整读取 4 字节并恢复单精度值时为 true，否则为 false。
		 */
		bool F32(float&);
		/**
		 * @brief 从当前游标取指定长度的字节视图并推进位置。
		 *
		 * @param[in] size 从当前读游标取出的字节数。
		 * @param[out] output 接收指向原始输入的只读区间，其寿命依赖输入缓冲区。
		 *
		 * @return 剩余字节充足且成功返回视图时为 true，否则为 false。
		 */
		bool Bytes(size_t, std::span<const std::byte>&);
		/**
		 * @brief 读取带 32 位长度前缀的字符串并检查字节预算。
		 * 读取 32 位长度前缀的字符串并校验长度上限。
		 *
		 * @param[out] output 接收按长度前缀读取的字符串内容。
		 * @param[in] maxBytes 允许读取的字符串最大字节数。
		 *
		 * @return 长度前缀及相应字节均可读且未超限时为 true，否则为 false。
		 */
		bool String(std::string&, size_t maxBytes = 4096);
		[[nodiscard]] bool AtEnd() const noexcept { return m_Offset == m_Data.size() && m_Error.empty(); }
		[[nodiscard]] size_t Remaining() const noexcept { return m_Data.size() - m_Offset; }
		[[nodiscard]] const std::string& Error() const noexcept { return m_Error; }

	private:
		std::span<const std::byte> m_Data;
		size_t m_Offset = 0;
		std::string m_Error;
	};
}
