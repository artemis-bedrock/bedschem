#pragma once

#include "bedschem/schematic.h"

#include <cstdint>

namespace bedschem {
	enum class Rotation : uint8_t {
		None,
		Clockwise90,
		Clockwise180,
		Clockwise270
	};

	enum class Axis : uint8_t {
		X,
		Z
	};

	[[nodiscard]] BlockState rotateState(const BlockState& state, Rotation rotation);
	[[nodiscard]] BlockState mirrorState(const BlockState& state, Axis axis);
	[[nodiscard]] Schematic rotate(const Schematic& schematic, Rotation rotation);
	[[nodiscard]] Schematic mirror(const Schematic& schematic, Axis axis);
}
