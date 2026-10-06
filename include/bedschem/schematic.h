#pragma once

#include "bedschem/nbt/tag.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bedschem {
	inline constexpr int32_t kVoid = -1;
	inline constexpr std::size_t kLayerCount = 2;

	struct BlockPos {
		int32_t x{};
		int32_t y{};
		int32_t z{};

		[[nodiscard]] bool operator==(const BlockPos& other) const = default;
	};

	struct BlockState {
		std::string name;
		nbt::Compound states;
		int32_t version{};

		[[nodiscard]] bool operator==(const BlockState& other) const = default;
	};

	struct PositionData {
		int32_t index{};
		nbt::Compound data;

		[[nodiscard]] bool operator==(const PositionData& other) const = default;
	};

	struct Schematic {
		BlockPos size{};
		BlockPos origin{};
		std::vector<BlockState> palette;
		std::array<std::vector<int32_t>, kLayerCount> layers;
		std::vector<PositionData> positionData;
		std::vector<nbt::Compound> entities;

		[[nodiscard]] static Schematic filled(BlockPos size, int32_t paletteIndex = kVoid);

		[[nodiscard]] int64_t volume() const noexcept;
		[[nodiscard]] bool contains(BlockPos position) const noexcept;
		[[nodiscard]] int32_t index(BlockPos position) const noexcept;
		[[nodiscard]] BlockPos position(int32_t index) const noexcept;
		[[nodiscard]] int32_t block(std::size_t layer, BlockPos position) const noexcept;
		void setBlock(std::size_t layer, BlockPos position, int32_t paletteIndex) noexcept;

		[[nodiscard]] bool operator==(const Schematic& other) const = default;
	};
}
