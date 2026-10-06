#include "bedschem/schematic.h"

namespace bedschem {
	Schematic Schematic::filled(const BlockPos size, const int32_t paletteIndex) {
		Schematic schematic{ .size = size };
		const auto volume = static_cast<std::size_t>(schematic.volume());
		schematic.layers[0].assign(volume, paletteIndex);
		schematic.layers[1].assign(volume, kVoid);
		return schematic;
	}

	int64_t Schematic::volume() const noexcept {
		if (size.x <= 0 || size.y <= 0 || size.z <= 0) {
			return 0;
		}

		return static_cast<int64_t>(size.x) * size.y * size.z;
	}

	bool Schematic::contains(const BlockPos position) const noexcept {
		const auto insideX = position.x >= 0 && position.x < size.x;
		const auto insideY = position.y >= 0 && position.y < size.y;
		const auto insideZ = position.z >= 0 && position.z < size.z;
		return insideX && insideY && insideZ;
	}

	int32_t Schematic::index(const BlockPos position) const noexcept {
		return (position.x * size.y + position.y) * size.z + position.z;
	}

	BlockPos Schematic::position(const int32_t index) const noexcept {
		return {
			.x = index / (size.y * size.z),
			.y = index / size.z % size.y,
			.z = index % size.z
		};
	}

	int32_t Schematic::block(const std::size_t layer, const BlockPos position) const noexcept {
		if (layer >= kLayerCount || !contains(position)) {
			return kVoid;
		}

		return layers[layer][static_cast<std::size_t>(index(position))];
	}

	void Schematic::setBlock(const std::size_t layer, const BlockPos position, const int32_t paletteIndex) noexcept {
		if (layer >= kLayerCount || !contains(position)) {
			return;
		}

		layers[layer][static_cast<std::size_t>(index(position))] = paletteIndex;
	}
}
