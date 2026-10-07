#pragma once

#include "bedschem/schematic.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace bedschem::java {
	struct Property {
		std::string_view name;
		std::string_view value;
	};

	struct Conversion {
		BlockState block;
		bool waterlogged{};
	};

	struct PaletteEntry {
		int32_t block{ kVoid };
		int32_t liquid{ kVoid };
	};

	[[nodiscard]] Conversion convert(std::string_view name, std::span<const Property> properties);
	[[nodiscard]] Conversion convert(std::string_view blockState);
	[[nodiscard]] PaletteEntry addToPalette(std::vector<BlockState>& palette, const Conversion& conversion);
	void place(Schematic& schematic, BlockPos position, PaletteEntry entry);
}
