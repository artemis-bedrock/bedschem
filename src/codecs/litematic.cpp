#include "codecs/litematic.h"

#include "failure.h"
#include "java/blocks.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace bedschem::codecs::litematic {
	static constexpr int32_t kMinimumBits = 2;
	static constexpr int32_t kLongBits = 64;

	struct Region {
		BlockPos minimum{};
		BlockPos size{};
		std::vector<java::PaletteEntry> palette;
		const nbt::LongArray* states{};
		int32_t bits{};
	};

	[[nodiscard]] static std::optional<BlockPos> readVector(const nbt::Compound& compound, const std::string_view name) {
		const auto* vector = compound.get<nbt::Compound>(name);
		const auto* x = vector ? vector->get<int32_t>("x") : nullptr;
		const auto* y = vector ? vector->get<int32_t>("y") : nullptr;
		const auto* z = vector ? vector->get<int32_t>("z") : nullptr;
		if (!x || !y || !z) {
			return std::nullopt;
		}

		return BlockPos{ .x = *x, .y = *y, .z = *z };
	}

	[[nodiscard]] static int32_t minimumCorner(const int32_t position, const int32_t size) {
		return size < 0 ? position + size + 1 : position;
	}

	[[nodiscard]] static int64_t volumeOf(const BlockPos size) {
		return static_cast<int64_t>(size.x) * size.y * size.z;
	}

	[[nodiscard]] static int32_t bitsFor(const std::size_t paletteSize) {
		const auto largest = std::max<std::size_t>(paletteSize, 1) - 1;
		return std::max(kMinimumBits, std::bit_width(largest));
	}

	[[nodiscard]] static std::expected<Region, Error> readRegion(const std::string& name, const nbt::Compound& region, std::vector<BlockState>& palette) {
		const auto position = readVector(region, "Position");
		const auto size = readVector(region, "Size");
		const auto* entries = region.get<nbt::List>("BlockStatePalette");
		const auto* states = region.get<nbt::LongArray>("BlockStates");
		if (!position || !size || !entries || !states) {
			return failure(ErrorCode::MissingField, "Regions." + name);
		}

		const BlockPos extent{ .x = std::abs(size->x), .y = std::abs(size->y), .z = std::abs(size->z) };
		const auto volume = volumeOf(extent);
		if (volume <= 0 || volume > std::numeric_limits<int32_t>::max()) {
			return failure(ErrorCode::InvalidField, "Regions." + name + ".Size");
		}

		Region result{
			.minimum = {
				.x = minimumCorner(position->x, size->x),
				.y = minimumCorner(position->y, size->y),
				.z = minimumCorner(position->z, size->z)
			},
			.size = extent,
			.states = states,
			.bits = bitsFor(entries->items.size())
		};

		result.palette.reserve(entries->items.size());
		for (const auto& item : entries->items) {
			const auto* entry = item.get<nbt::Compound>();
			if (!entry) {
				return failure(ErrorCode::InvalidField, "Regions." + name + ".BlockStatePalette");
			}

			result.palette.push_back(java::addToPalette(palette, java::convert(*entry)));
		}

		if (const auto requiredLongs = (volume * result.bits + kLongBits - 1) / kLongBits; static_cast<int64_t>(states->size()) < requiredLongs) {
			return failure(ErrorCode::InvalidLength, "Regions." + name + ".BlockStates");
		}

		return result;
	}

	[[nodiscard]] static uint64_t packedValue(const nbt::LongArray& states, const int64_t index, const int32_t bits) {
		const auto mask = (uint64_t{ 1 } << bits) - 1;
		const auto startBit = index * bits;
		const auto startLong = static_cast<std::size_t>(startBit / kLongBits);
		const auto endLong = static_cast<std::size_t>((startBit + bits - 1) / kLongBits);
		const auto offset = static_cast<int32_t>(startBit % kLongBits);
		const auto low = static_cast<uint64_t>(states[startLong]) >> offset;
		if (startLong == endLong) {
			return low & mask;
		}

		const auto high = static_cast<uint64_t>(states[endLong]) << (kLongBits - offset);
		return (low | high) & mask;
	}

	static void placeRegion(Schematic& schematic, const Region& region, const BlockPos origin) {
		const auto layerSize = static_cast<int64_t>(region.size.x) * region.size.z;
		const auto volume = layerSize * region.size.y;
		for (int64_t index = 0; index < volume; ++index) {
			const auto value = packedValue(*region.states, index, region.bits);
			if (value >= region.palette.size()) {
				continue;
			}

			const BlockPos position{
				.x = region.minimum.x - origin.x + static_cast<int32_t>(index % region.size.x),
				.y = region.minimum.y - origin.y + static_cast<int32_t>(index / layerSize),
				.z = region.minimum.z - origin.z + static_cast<int32_t>(index % layerSize / region.size.x)
			};

			java::place(schematic, position, region.palette[static_cast<std::size_t>(value)]);
		}
	}

	bool matches(const nbt::Compound& root) {
		return root.contains("Regions");
	}

	std::expected<Schematic, Error> read(const nbt::Compound& root) {
		const auto* regions = root.get<nbt::Compound>("Regions");
		if (!regions || regions->empty()) {
			return failure(ErrorCode::MissingField, "Regions");
		}

		std::vector<BlockState> palette;
		std::vector<Region> parsed;
		for (const auto& [name, tag] : *regions) {
			const auto* region = tag.get<nbt::Compound>();
			if (!region) {
				return failure(ErrorCode::InvalidField, "Regions." + name);
			}

			auto result = readRegion(name, *region, palette);
			if (!result) {
				return std::unexpected(result.error());
			}

			parsed.push_back(std::move(*result));
		}

		auto minimum = parsed.front().minimum;
		auto maximum = minimum;
		for (const auto& region : parsed) {
			minimum = { .x = std::min(minimum.x, region.minimum.x), .y = std::min(minimum.y, region.minimum.y), .z = std::min(minimum.z, region.minimum.z) };
			maximum = {
				.x = std::max(maximum.x, region.minimum.x + region.size.x - 1),
				.y = std::max(maximum.y, region.minimum.y + region.size.y - 1),
				.z = std::max(maximum.z, region.minimum.z + region.size.z - 1)
			};
		}

		const BlockPos size{ .x = maximum.x - minimum.x + 1, .y = maximum.y - minimum.y + 1, .z = maximum.z - minimum.z + 1 };
		if (volumeOf(size) > std::numeric_limits<int32_t>::max()) {
			return failure(ErrorCode::InvalidField, "Regions");
		}

		auto schematic = Schematic::filled(size);
		schematic.palette = std::move(palette);
		for (const auto& region : parsed) {
			placeRegion(schematic, region, minimum);
		}

		return schematic;
	}
}
