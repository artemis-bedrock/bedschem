#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace bedschem::nbt {
	enum class TagType : uint8_t {
		End,
		Byte,
		Short,
		Int,
		Long,
		Float,
		Double,
		ByteArray,
		String,
		List,
		Compound,
		IntArray,
		LongArray
	};

	enum class Endian : uint8_t {
		Little,
		Big
	};

	struct Tag;

	using ByteArray = std::vector<int8_t>;
	using IntArray = std::vector<int32_t>;
	using LongArray = std::vector<int64_t>;

	struct List {
		TagType type{ TagType::End };
		std::vector<Tag> items;

		[[nodiscard]] bool operator==(const List& other) const;
	};

	class Compound {
	public:
		using Entry = std::pair<std::string, Tag>;
		using const_iterator = std::vector<Entry>::const_iterator;

		Compound() = default;
		explicit Compound(std::vector<Entry> entries);

		[[nodiscard]] const Tag* find(std::string_view name) const;
		[[nodiscard]] Tag* find(std::string_view name);
		[[nodiscard]] bool contains(std::string_view name) const;
		Tag& set(std::string name, Tag value);
		bool erase(std::string_view name);

		template <typename Value>
		[[nodiscard]] auto* get(this auto&& self, std::string_view name) {
			auto* tag = self.find(name);
			return tag ? tag->template get<Value>() : nullptr;
		}

		[[nodiscard]] std::size_t size() const noexcept;
		[[nodiscard]] bool empty() const noexcept;
		[[nodiscard]] const_iterator begin() const noexcept;
		[[nodiscard]] const_iterator end() const noexcept;

		[[nodiscard]] bool operator==(const Compound& other) const;

	private:
		std::vector<Entry> mEntries;
	};

	struct Tag {
		using Value = std::variant<
			int8_t,
			int16_t,
			int32_t,
			int64_t,
			float,
			double,
			ByteArray,
			std::string,
			List,
			Compound,
			IntArray,
			LongArray
		>;

		Tag() = default;

		template <typename Type>
		requires(!std::same_as<std::remove_cvref_t<Type>, Tag> && std::constructible_from<Value, Type>)
		Tag(Type&& initial) : value(std::forward<Type>(initial)) { }

		[[nodiscard]] TagType type() const noexcept;

		template <typename Type>
		[[nodiscard]] auto* get(this auto&& self) noexcept {
			return std::get_if<Type>(&self.value);
		}

		[[nodiscard]] bool operator==(const Tag& other) const = default;

		Value value;
	};
}
