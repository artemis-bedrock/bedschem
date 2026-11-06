#pragma once

#include "bedschem/error.h"
#include "bedschem/schematic.h"

#include <nbt.h>
#include <expected>

namespace bedschem::codecs::litematic {
	[[nodiscard]] bool matches(const nbt::Compound& root);
	[[nodiscard]] std::expected<Schematic, Error> read(const nbt::Compound& root);
}
