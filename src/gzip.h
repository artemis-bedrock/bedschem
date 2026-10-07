#pragma once

#include "bedschem/error.h"

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

namespace bedschem {
	[[nodiscard]] std::expected<std::vector<std::byte>, Error> decompressGzip(std::span<const std::byte> data);
}
