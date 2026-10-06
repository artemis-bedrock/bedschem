#pragma once

#include "bedschem/error.h"
#include "bedschem/nbt/tag.h"

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bedschem::nbt {
	struct NamedCompound {
		std::string name;
		Compound compound;
	};

	[[nodiscard]] std::expected<NamedCompound, Error> read(std::span<const std::byte> data, Endian endian);
	[[nodiscard]] std::vector<std::byte> write(const Compound& compound, Endian endian, std::string_view name = {});
}
