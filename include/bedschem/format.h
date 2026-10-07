#pragma once

#include "bedschem/error.h"
#include "bedschem/schematic.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bedschem {
	enum class Format : uint8_t {
		McStructure,
		Litematic,
		Sponge
	};

	struct FormatInfo {
		Format format{};
		std::string_view name;
		std::string_view extension;
		bool writable{};
	};

	[[nodiscard]] std::span<const FormatInfo> formats();
	[[nodiscard]] const FormatInfo& formatInfo(Format format);
	[[nodiscard]] std::optional<Format> formatForExtension(std::string_view extension);

	[[nodiscard]] std::expected<Schematic, Error> read(std::span<const std::byte> data);
	[[nodiscard]] std::expected<Schematic, Error> read(std::span<const std::byte> data, Format format);
	[[nodiscard]] std::expected<std::vector<std::byte>, Error> write(const Schematic& schematic, Format format);
}
