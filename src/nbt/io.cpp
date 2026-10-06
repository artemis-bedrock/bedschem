#include "bedschem/nbt/io.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <optional>
#include <type_traits>

namespace bedschem::nbt {
	namespace detail {
		constexpr int max_depth = 512;

		[[nodiscard]] static std::size_t minimum_payload_size(const TagType type) {
			switch (type) {
				case TagType::End:
					return 0;
				case TagType::Byte:
				case TagType::Compound:
					return 1;
				case TagType::Short:
				case TagType::String:
					return 2;
				case TagType::Int:
				case TagType::Float:
				case TagType::ByteArray:
				case TagType::IntArray:
				case TagType::LongArray:
					return 4;
				case TagType::List:
					return 5;
				case TagType::Long:
				case TagType::Double:
					return 8;
			}

			return 0;
		}

		[[nodiscard]] static bool needs_swap(const Endian endian) {
			const auto little = endian == Endian::Little;
			return little != (std::endian::native == std::endian::little);
		}

		class byte_reader {
		public:
			byte_reader(const std::span<const std::byte> data, const Endian endian) : m_data(data), m_swap(needs_swap(endian)) { }

			[[nodiscard]] const std::optional<Error>& error() const {
				return m_error;
			}

			template <typename Value>
			[[nodiscard]] Value read_value() {
				if (remaining() < sizeof(Value)) {
					fail(ErrorCode::UnexpectedEnd);
					return Value{};
				}

				std::array<std::byte, sizeof(Value)> bytes{};
				std::memcpy(bytes.data(), m_data.data() + m_offset, sizeof(Value));
				m_offset += sizeof(Value);
				if (m_swap) {
					std::ranges::reverse(bytes);
				}

				return std::bit_cast<Value>(bytes);
			}

			[[nodiscard]] TagType read_type() {
				const auto raw = read_value<uint8_t>();
				if (raw > static_cast<uint8_t>(TagType::LongArray)) {
					fail(ErrorCode::InvalidTagType);
					return TagType::End;
				}

				return static_cast<TagType>(raw);
			}

			[[nodiscard]] std::string read_string() {
				const auto length = read_value<uint16_t>();
				if (remaining() < length) {
					fail(ErrorCode::UnexpectedEnd);
					return {};
				}

				std::string text(reinterpret_cast<const char*>(m_data.data() + m_offset), length);
				m_offset += length;
				return text;
			}

			[[nodiscard]] std::size_t read_length(const std::size_t element_size) {
				const auto length = read_value<int32_t>();
				if (length < 0) {
					fail(ErrorCode::InvalidLength);
					return 0;
				}

				const auto count = static_cast<std::size_t>(length);
				if (element_size != 0 && count > remaining() / element_size) {
					fail(ErrorCode::UnexpectedEnd);
					return 0;
				}

				return count;
			}

			template <typename Element>
			[[nodiscard]] std::vector<Element> read_array() {
				const auto count = read_length(sizeof(Element));
				std::vector<Element> values;
				values.reserve(count);
				for (std::size_t i = 0; i < count && !m_error; ++i) {
					values.push_back(read_value<Element>());
				}

				return values;
			}

			[[nodiscard]] Tag read_payload(const TagType type, const int depth) {
				switch (type) {
					case TagType::Byte:
						return read_value<int8_t>();
					case TagType::Short:
						return read_value<int16_t>();
					case TagType::Int:
						return read_value<int32_t>();
					case TagType::Long:
						return read_value<int64_t>();
					case TagType::Float:
						return read_value<float>();
					case TagType::Double:
						return read_value<double>();
					case TagType::ByteArray:
						return read_array<int8_t>();
					case TagType::String:
						return read_string();
					case TagType::List:
						return read_list(depth);
					case TagType::Compound:
						return read_compound(depth);
					case TagType::IntArray:
						return read_array<int32_t>();
					case TagType::LongArray:
						return read_array<int64_t>();
					case TagType::End:
						break;
				}

				fail(ErrorCode::InvalidTagType);
				return {};
			}

			[[nodiscard]] List read_list(const int depth) {
				if (depth >= max_depth) {
					fail(ErrorCode::DepthExceeded);
					return {};
				}

				List list{ .type = read_type() };
				const auto count = read_length(minimum_payload_size(list.type));
				if (list.type == TagType::End && count != 0) {
					fail(ErrorCode::InvalidTagType);
					return {};
				}

				list.items.reserve(count);
				for (std::size_t i = 0; i < count && !m_error; ++i) {
					list.items.push_back(read_payload(list.type, depth + 1));
				}

				return list;
			}

			[[nodiscard]] Compound read_compound(const int depth) {
				if (depth >= max_depth) {
					fail(ErrorCode::DepthExceeded);
					return {};
				}

				std::vector<Compound::Entry> entries;
				while (!m_error) {
					const auto type = read_type();
					if (type == TagType::End) {
						break;
					}

					auto name = read_string();
					auto value = read_payload(type, depth + 1);
					entries.emplace_back(std::move(name), std::move(value));
				}

				return Compound{ std::move(entries) };
			}

		private:
			[[nodiscard]] std::size_t remaining() const {
				return m_data.size() - m_offset;
			}

			void fail(const ErrorCode code) {
				if (!m_error) {
					m_error = Error{ .code = code, .context = "offset " + std::to_string(m_offset) };
				}

				m_offset = m_data.size();
			}

			std::span<const std::byte> m_data;
			std::size_t m_offset{};
			bool m_swap{};
			std::optional<Error> m_error;
		};

		class byte_writer {
		public:
			explicit byte_writer(const Endian endian) : m_swap(needs_swap(endian)) { }

			[[nodiscard]] std::vector<std::byte> take() {
				return std::move(m_bytes);
			}

			template <typename Value>
			void write_value(const Value value) {
				auto bytes = std::bit_cast<std::array<std::byte, sizeof(Value)>>(value);
				if (m_swap) {
					std::ranges::reverse(bytes);
				}

				m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
			}

			void write_type(const TagType type) {
				write_value(static_cast<uint8_t>(type));
			}

			void write_string(const std::string_view text) {
				const auto length = static_cast<uint16_t>(std::min<std::size_t>(text.size(), UINT16_MAX));
				write_value(length);
				const auto* begin = reinterpret_cast<const std::byte*>(text.data());
				m_bytes.insert(m_bytes.end(), begin, begin + length);
			}

			void write_payload(const Tag& tag) {
				std::visit([this](const auto& value) {
					write_alternative(value);
				}, tag.value);
			}

			void write_compound(const Compound& compound) {
				for (const auto& [name, tag] : compound) {
					write_type(tag.type());
					write_string(name);
					write_payload(tag);
				}

				write_type(TagType::End);
			}

		private:
			void write_list(const List& list) {
				const auto type = list.items.empty() ? list.type : list.items.front().type();
				write_type(type);
				write_value(static_cast<int32_t>(list.items.size()));
				for (const auto& item : list.items) {
					write_payload(item);
				}
			}

			template <typename Element>
			void write_array(const std::vector<Element>& values) {
				write_value(static_cast<int32_t>(values.size()));
				for (const auto value : values) {
					write_value(value);
				}
			}

			template <typename Value>
			void write_alternative(const Value& value) {
				if constexpr (std::is_arithmetic_v<Value>) {
					write_value(value);
				} else if constexpr (std::is_same_v<Value, std::string>) {
					write_string(value);
				} else if constexpr (std::is_same_v<Value, List>) {
					write_list(value);
				} else if constexpr (std::is_same_v<Value, Compound>) {
					write_compound(value);
				} else {
					write_array(value);
				}
			}

			std::vector<std::byte> m_bytes;
			bool m_swap{};
		};
	}

	std::expected<NamedCompound, Error> read(const std::span<const std::byte> data, const Endian endian) {
		detail::byte_reader reader{ data, endian };
		const auto type = reader.read_type();
		if (reader.error()) {
			return std::unexpected(*reader.error());
		}

		if (type != TagType::Compound) {
			return std::unexpected(Error{ .code = ErrorCode::RootNotCompound });
		}

		auto name = reader.read_string();
		auto compound = reader.read_compound(0);
		if (reader.error()) {
			return std::unexpected(*reader.error());
		}

		return NamedCompound{
			.name = std::move(name),
			.compound = std::move(compound)
		};
	}

	std::vector<std::byte> write(const Compound& compound, const Endian endian, const std::string_view name) {
		detail::byte_writer writer{ endian };
		writer.write_type(TagType::Compound);
		writer.write_string(name);
		writer.write_compound(compound);
		return writer.take();
	}
}
