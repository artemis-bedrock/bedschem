#include "bedschem/transform.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace bedschem {
	enum class Direction : uint8_t {
		Down,
		Up,
		North,
		South,
		West,
		East
	};

	using namespace std::string_view_literals;

	static constexpr auto kDirectionNames = std::to_array({
		"down"sv,
		"up"sv,
		"north"sv,
		"south"sv,
		"west"sv,
		"east"sv
	});

	static constexpr auto kClockwiseDirections = std::to_array({
		Direction::North,
		Direction::East,
		Direction::South,
		Direction::West
	});

	static constexpr auto kWeirdoDirections = std::to_array({
		Direction::East,
		Direction::West,
		Direction::South,
		Direction::North
	});

	static constexpr auto kNamedDirectionStates = std::to_array({
		"minecraft:cardinal_direction"sv,
		"minecraft:facing_direction"sv,
		"minecraft:block_face"sv,
		"torch_facing_direction"sv
	});

	static constexpr auto kAxisStates = std::to_array({
		"pillar_axis"sv,
		"portal_axis"sv
	});

	static constexpr int32_t kSignDirections = 16;

	[[nodiscard]] static int quarterTurns(const Rotation rotation) {
		return static_cast<int>(rotation);
	}

	[[nodiscard]] static Direction rotateDirection(const Direction direction, const Rotation rotation) {
		const auto current = std::ranges::find(kClockwiseDirections, direction);
		if (current == kClockwiseDirections.end()) {
			return direction;
		}

		const auto offset = (current - kClockwiseDirections.begin() + quarterTurns(rotation)) % std::ssize(kClockwiseDirections);
		return kClockwiseDirections[static_cast<std::size_t>(offset)];
	}

	[[nodiscard]] static Direction mirrorDirection(const Direction direction, const Axis axis) {
		if (axis == Axis::X) {
			if (direction == Direction::East) {
				return Direction::West;
			}

			return direction == Direction::West ? Direction::East : direction;
		}

		if (direction == Direction::North) {
			return Direction::South;
		}

		return direction == Direction::South ? Direction::North : direction;
	}

	[[nodiscard]] static std::optional<Direction> directionNamed(const std::string_view name) {
		const auto found = std::ranges::find(kDirectionNames, name);
		if (found == kDirectionNames.end()) {
			return std::nullopt;
		}

		return static_cast<Direction>(found - kDirectionNames.begin());
	}

	template <typename MapDirection>
	static void remapNamedDirections(nbt::Compound& states, const MapDirection& mapDirection) {
		for (const auto name : kNamedDirectionStates) {
			auto* value = states.get<std::string>(name);
			const auto direction = value ? directionNamed(*value) : std::nullopt;
			if (direction) {
				*value = kDirectionNames[static_cast<std::size_t>(mapDirection(*direction))];
			}
		}
	}

	template <typename MapDirection>
	static void remapFacingDirection(nbt::Compound& states, const MapDirection& mapDirection) {
		auto* value = states.get<int32_t>("facing_direction");
		if (value && *value >= 0 && *value < std::ssize(kDirectionNames)) {
			*value = static_cast<int32_t>(mapDirection(static_cast<Direction>(*value)));
		}
	}

	template <typename MapDirection>
	static void remapWeirdoDirection(nbt::Compound& states, const MapDirection& mapDirection) {
		auto* value = states.get<int32_t>("weirdo_direction");
		if (!value || *value < 0 || *value >= std::ssize(kWeirdoDirections)) {
			return;
		}

		const auto mapped = mapDirection(kWeirdoDirections[static_cast<std::size_t>(*value)]);
		*value = static_cast<int32_t>(std::ranges::find(kWeirdoDirections, mapped) - kWeirdoDirections.begin());
	}

	template <typename MapSign>
	static void remapSignDirection(nbt::Compound& states, const MapSign& mapSign) {
		auto* value = states.get<int32_t>("ground_sign_direction");
		if (value && *value >= 0 && *value < kSignDirections) {
			*value = mapSign(*value) % kSignDirections;
		}
	}

	static void swapHorizontalAxes(nbt::Compound& states) {
		for (const auto name : kAxisStates) {
			auto* value = states.get<std::string>(name);
			if (!value) {
				continue;
			}

			if (*value == "x") {
				*value = "z";
			} else if (*value == "z") {
				*value = "x";
			}
		}
	}

	static void flipDoorHinge(nbt::Compound& states) {
		auto* value = states.get<int8_t>("door_hinge_bit");
		if (value) {
			*value = static_cast<int8_t>(*value == 0 ? 1 : 0);
		}
	}

	[[nodiscard]] static BlockPos rotatedSize(const BlockPos size, const Rotation rotation) {
		if (quarterTurns(rotation) % 2 == 0) {
			return size;
		}

		return { .x = size.z, .y = size.y, .z = size.x };
	}

	[[nodiscard]] static BlockPos rotatePosition(const BlockPos position, const BlockPos size, const Rotation rotation) {
		switch (rotation) {
			case Rotation::Clockwise90:
				return { .x = size.z - 1 - position.z, .y = position.y, .z = position.x };
			case Rotation::Clockwise180:
				return { .x = size.x - 1 - position.x, .y = position.y, .z = size.z - 1 - position.z };
			case Rotation::Clockwise270:
				return { .x = position.z, .y = position.y, .z = size.x - 1 - position.x };
			case Rotation::None:
				break;
		}

		return position;
	}

	[[nodiscard]] static BlockPos mirrorPosition(const BlockPos position, const BlockPos size, const Axis axis) {
		if (axis == Axis::X) {
			return { .x = size.x - 1 - position.x, .y = position.y, .z = position.z };
		}

		return { .x = position.x, .y = position.y, .z = size.z - 1 - position.z };
	}

	template <typename MapPosition, typename MapState>
	[[nodiscard]] static Schematic remap(const Schematic& source, const BlockPos size, const MapPosition& mapPosition, const MapState& mapState) {
		auto result = Schematic::filled(size);
		result.origin = source.origin;
		result.entities = source.entities;
		result.palette.reserve(source.palette.size());
		for (const auto& state : source.palette) {
			result.palette.push_back(mapState(state));
		}

		const auto volume = static_cast<int32_t>(source.volume());
		for (std::size_t layer = 0; layer < kLayerCount; ++layer) {
			if (source.layers[layer].size() != static_cast<std::size_t>(volume)) {
				continue;
			}

			for (int32_t index = 0; index < volume; ++index) {
				const auto target = result.index(mapPosition(source.position(index)));
				result.layers[layer][static_cast<std::size_t>(target)] = source.layers[layer][static_cast<std::size_t>(index)];
			}
		}

		result.positionData.reserve(source.positionData.size());
		for (const auto& [index, data] : source.positionData) {
			result.positionData.push_back({
				.index = result.index(mapPosition(source.position(index))),
				.data = data
			});
		}

		std::ranges::sort(result.positionData, {}, &PositionData::index);
		return result;
	}

	BlockState rotateState(const BlockState& state, const Rotation rotation) {
		auto result = state;
		const auto mapDirection = [rotation](const Direction direction) {
			return rotateDirection(direction, rotation);
		};

		remapNamedDirections(result.states, mapDirection);
		remapFacingDirection(result.states, mapDirection);
		remapWeirdoDirection(result.states, mapDirection);
		remapSignDirection(result.states, [rotation](const int32_t value) {
			return value + quarterTurns(rotation) * kSignDirections / 4;
		});

		if (quarterTurns(rotation) % 2 != 0) {
			swapHorizontalAxes(result.states);
		}

		return result;
	}

	BlockState mirrorState(const BlockState& state, const Axis axis) {
		auto result = state;
		const auto mapDirection = [axis](const Direction direction) {
			return mirrorDirection(direction, axis);
		};

		remapNamedDirections(result.states, mapDirection);
		remapFacingDirection(result.states, mapDirection);
		remapWeirdoDirection(result.states, mapDirection);
		remapSignDirection(result.states, [axis](const int32_t value) {
			const auto reflection = axis == Axis::X ? kSignDirections : kSignDirections / 2;
			return reflection - value + kSignDirections;
		});

		flipDoorHinge(result.states);
		return result;
	}

	Schematic rotate(const Schematic& schematic, const Rotation rotation) {
		const auto size = rotatedSize(schematic.size, rotation);
		return remap(schematic, size, [&schematic, rotation](const BlockPos position) {
			return rotatePosition(position, schematic.size, rotation);
		}, [rotation](const BlockState& state) {
			return rotateState(state, rotation);
		});
	}

	Schematic mirror(const Schematic& schematic, const Axis axis) {
		return remap(schematic, schematic.size, [&schematic, axis](const BlockPos position) {
			return mirrorPosition(position, schematic.size, axis);
		}, [axis](const BlockState& state) {
			return mirrorState(state, axis);
		});
	}
}
