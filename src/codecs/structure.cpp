#include "codecs/structure.h"

#include "failure.h"
#include "java/blocks.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace bedschem::codecs::structure {
	[[nodiscard]] static std::optional<BlockPos> readVector(const nbt::Compound& compound, const std::string_view name) {
		const auto* list = compound.get<nbt::List>(name);
		if (!list || list->items.size() != 3) {
			return std::nullopt;
		}

		std::array<int32_t, 3> values{};
		for (std::size_t axis = 0; axis < values.size(); ++axis) {
			const auto* value = list->items[axis].get<int32_t>();
			if (!value) {
				return std::nullopt;
			}

			values[axis] = *value;
		}

		return BlockPos{ .x = values[0], .y = values[1], .z = values[2] };
	}

	[[nodiscard]] static const nbt::List* paletteOf(const nbt::Compound& root) {
		if (const auto* palette = root.get<nbt::List>("palette")) {
			return palette;
		}

		const auto* palettes = root.get<nbt::List>("palettes");
		if (!palettes || palettes->items.empty()) {
			return nullptr;
		}

		return palettes->items.front().get<nbt::List>();
	}

	[[nodiscard]] static std::expected<std::vector<java::PaletteEntry>, Error> readPalette(const nbt::List& entries, std::vector<BlockState>& palette) {
		std::vector<java::PaletteEntry> converted;
		converted.reserve(entries.items.size());
		for (const auto& item : entries.items) {
			const auto* entry = item.get<nbt::Compound>();
			if (!entry) {
				return failure(ErrorCode::InvalidField, "palette[" + std::to_string(converted.size()) + "]");
			}

			converted.push_back(java::addToPalette(palette, java::convert(*entry)));
		}

		return converted;
	}

	bool matches(const nbt::Compound& root) {
		return root.contains("size") && root.contains("blocks") && (root.contains("palette") || root.contains("palettes"));
	}

	std::expected<Schematic, Error> read(const nbt::Compound& root) {
		const auto size = readVector(root, "size");
		if (!size) {
			return failure(ErrorCode::MissingField, "size");
		}

		const auto volume = static_cast<int64_t>(size->x) * size->y * size->z;
		if (size->x <= 0 || size->y <= 0 || size->z <= 0 || volume > std::numeric_limits<int32_t>::max()) {
			return failure(ErrorCode::InvalidField, "size");
		}

		const auto* entries = paletteOf(root);
		const auto* blocks = root.get<nbt::List>("blocks");
		if (!entries || !blocks) {
			return failure(ErrorCode::MissingField, "palette or blocks");
		}

		auto schematic = Schematic::filled(*size);
		const auto palette = readPalette(*entries, schematic.palette);
		if (!palette) {
			return std::unexpected(palette.error());
		}

		for (std::size_t index = 0; index < blocks->items.size(); ++index) {
			const auto* block = blocks->items[index].get<nbt::Compound>();
			const auto position = block ? readVector(*block, "pos") : std::nullopt;
			const auto* state = block ? block->get<int32_t>("state") : nullptr;
			const auto validState = state && *state >= 0 && static_cast<std::size_t>(*state) < palette->size();
			if (!position || !validState || !schematic.contains(*position)) {
				return failure(ErrorCode::InvalidField, "blocks[" + std::to_string(index) + "]");
			}

			java::place(schematic, *position, (*palette)[static_cast<std::size_t>(*state)]);
		}

		return schematic;
	}
}
