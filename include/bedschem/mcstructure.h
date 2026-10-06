#pragma once

#include "bedschem/error.h"
#include "bedschem/schematic.h"

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

namespace bedschem {
	[[nodiscard]] std::expected<Schematic, Error> readMcStructure(std::span<const std::byte> data);
	[[nodiscard]] std::vector<std::byte> writeMcStructure(const Schematic& schematic);
}
