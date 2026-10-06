#include "bedschem/mcstructure.h"

#include "bedschem/nbt/io.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <optional>
#include <string>

namespace bedschem {
	static constexpr int32_t kFormatVersion = 1;

	[[nodiscard]] static std::unexpected<Error> failure(const ErrorCode code, std::string context) {
		return std::unexpected(Error{ .code = code, .context = std::move(context) });
	}

	[[nodiscard]] static std::optional<BlockPos> readPosition(const nbt::Compound& compound, const std::string_view name) {
		const auto* list = compound.get<nbt::List>(name);
		if (!list || list->items.size() != 3) {
			return std::nullopt;
		}

		std::array<int32_t, 3> values{};
		for (std::size_t i = 0; i < values.size(); ++i) {
			const auto* value = list->items[i].get<int32_t>();
			if (!value) {
				return std::nullopt;
			}

			values[i] = *value;
		}

		return BlockPos{ .x = values[0], .y = values[1], .z = values[2] };
	}

	[[nodiscard]] static bool isValidSize(const BlockPos size) {
		if (size.x <= 0 || size.y <= 0 || size.z <= 0) {
			return false;
		}

		const auto volume = static_cast<int64_t>(size.x) * size.y * size.z;
		return volume <= std::numeric_limits<int32_t>::max();
	}

	[[nodiscard]] static std::optional<BlockState> readBlockState(const nbt::Tag& tag) {
		const auto* compound = tag.get<nbt::Compound>();
		if (!compound) {
			return std::nullopt;
		}

		const auto* name = compound->get<std::string>("name");
		if (!name) {
			return std::nullopt;
		}

		const auto* states = compound->get<nbt::Compound>("states");
		const auto* version = compound->get<int32_t>("version");
		return BlockState{
			.name = *name,
			.states = states ? *states : nbt::Compound{},
			.version = version ? *version : 0
		};
	}

	[[nodiscard]] static std::expected<std::vector<BlockState>, Error> readPalette(const nbt::Compound* palette) {
		std::vector<BlockState> states;
		if (!palette) {
			return states;
		}

		const auto* blockPalette = palette->get<nbt::List>("block_palette");
		if (!blockPalette) {
			return states;
		}

		states.reserve(blockPalette->items.size());
		for (const auto& entry : blockPalette->items) {
			auto state = readBlockState(entry);
			if (!state) {
				return failure(ErrorCode::InvalidField, "block_palette[" + std::to_string(states.size()) + "]");
			}

			states.push_back(std::move(*state));
		}

		return states;
	}

	[[nodiscard]] static std::expected<std::vector<int32_t>, Error> readLayer(const nbt::Tag& tag, const std::size_t volume, const std::size_t paletteSize) {
		const auto* list = tag.get<nbt::List>();
		if (!list || list->items.size() != volume) {
			return failure(ErrorCode::InvalidField, "block_indices");
		}

		std::vector<int32_t> layer;
		layer.reserve(volume);
		for (const auto& item : list->items) {
			const auto* value = item.get<int32_t>();
			const auto inPalette = value && *value >= kVoid && *value < static_cast<int64_t>(paletteSize);
			if (!inPalette) {
				return failure(ErrorCode::InvalidField, "block_indices[" + std::to_string(layer.size()) + "]");
			}

			layer.push_back(*value);
		}

		return layer;
	}

	[[nodiscard]] static std::expected<std::vector<PositionData>, Error> readPositionData(const nbt::Compound* palette, const int64_t volume) {
		std::vector<PositionData> entries;
		const auto* positionData = palette ? palette->get<nbt::Compound>("block_position_data") : nullptr;
		if (!positionData) {
			return entries;
		}

		entries.reserve(positionData->size());
		for (const auto& [key, tag] : *positionData) {
			int32_t index{};
			const auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), index);
			const auto validKey = error == std::errc{} && end == key.data() + key.size() && index >= 0 && index < volume;
			const auto* data = tag.get<nbt::Compound>();
			if (!validKey || !data) {
				return failure(ErrorCode::InvalidField, "block_position_data." + key);
			}

			entries.push_back({ .index = index, .data = *data });
		}

		std::ranges::sort(entries, {}, &PositionData::index);
		return entries;
	}

	[[nodiscard]] static std::expected<std::vector<nbt::Compound>, Error> readEntities(const nbt::Compound& structure) {
		std::vector<nbt::Compound> entities;
		const auto* list = structure.get<nbt::List>("entities");
		if (!list) {
			return entities;
		}

		entities.reserve(list->items.size());
		for (const auto& item : list->items) {
			const auto* entity = item.get<nbt::Compound>();
			if (!entity) {
				return failure(ErrorCode::InvalidField, "entities");
			}

			entities.push_back(*entity);
		}

		return entities;
	}

	[[nodiscard]] static std::expected<Schematic, Error> readStructure(const nbt::Compound& root) {
		const auto size = readPosition(root, "size");
		if (!size) {
			return failure(ErrorCode::MissingField, "size");
		}

		if (!isValidSize(*size)) {
			return failure(ErrorCode::InvalidField, "size");
		}

		const auto* structure = root.get<nbt::Compound>("structure");
		if (!structure) {
			return failure(ErrorCode::MissingField, "structure");
		}

		const auto* indices = structure->get<nbt::List>("block_indices");
		if (!indices) {
			return failure(ErrorCode::MissingField, "block_indices");
		}

		if (indices->items.empty() || indices->items.size() > kLayerCount) {
			return failure(ErrorCode::InvalidField, "block_indices");
		}

		const auto* palettes = structure->get<nbt::Compound>("palette");
		const auto* palette = palettes ? palettes->get<nbt::Compound>("default") : nullptr;
		auto states = readPalette(palette);
		if (!states) {
			return std::unexpected(states.error());
		}

		Schematic schematic = Schematic::filled(*size);
		schematic.origin = readPosition(root, "structure_world_origin").value_or(BlockPos{});
		schematic.palette = std::move(*states);

		const auto volume = static_cast<std::size_t>(schematic.volume());
		for (std::size_t layer = 0; layer < indices->items.size(); ++layer) {
			auto values = readLayer(indices->items[layer], volume, schematic.palette.size());
			if (!values) {
				return std::unexpected(values.error());
			}

			schematic.layers[layer] = std::move(*values);
		}

		auto positionData = readPositionData(palette, schematic.volume());
		if (!positionData) {
			return std::unexpected(positionData.error());
		}

		auto entities = readEntities(*structure);
		if (!entities) {
			return std::unexpected(entities.error());
		}

		schematic.positionData = std::move(*positionData);
		schematic.entities = std::move(*entities);
		return schematic;
	}

	[[nodiscard]] static nbt::List positionList(const BlockPos position) {
		return {
			.type = nbt::TagType::Int,
			.items = { position.x, position.y, position.z }
		};
	}

	[[nodiscard]] static nbt::List layerList(const std::vector<int32_t>& layer, const std::size_t volume) {
		nbt::List list{ .type = nbt::TagType::Int };
		list.items.reserve(volume);
		const auto complete = layer.size() == volume;
		for (std::size_t i = 0; i < volume; ++i) {
			list.items.emplace_back(complete ? layer[i] : kVoid);
		}

		return list;
	}

	[[nodiscard]] static nbt::Compound blockStateCompound(const BlockState& state) {
		nbt::Compound compound;
		compound.set("name", state.name);
		compound.set("states", state.states);
		compound.set("version", state.version);
		return compound;
	}

	[[nodiscard]] static nbt::Compound paletteCompound(const Schematic& schematic) {
		nbt::List blockPalette{ .type = nbt::TagType::Compound };
		blockPalette.items.reserve(schematic.palette.size());
		for (const auto& state : schematic.palette) {
			blockPalette.items.emplace_back(blockStateCompound(state));
		}

		nbt::Compound positionData;
		for (const auto& entry : schematic.positionData) {
			positionData.set(std::to_string(entry.index), entry.data);
		}

		nbt::Compound palette;
		palette.set("block_palette", std::move(blockPalette));
		palette.set("block_position_data", std::move(positionData));
		return palette;
	}

	[[nodiscard]] static nbt::Compound structureCompound(const Schematic& schematic) {
		const auto volume = static_cast<std::size_t>(schematic.volume());
		nbt::List indices{ .type = nbt::TagType::List };
		for (const auto& layer : schematic.layers) {
			indices.items.emplace_back(layerList(layer, volume));
		}

		nbt::List entities{ .type = nbt::TagType::Compound };
		entities.items.assign(schematic.entities.begin(), schematic.entities.end());

		nbt::Compound palettes;
		palettes.set("default", paletteCompound(schematic));

		nbt::Compound structure;
		structure.set("block_indices", std::move(indices));
		structure.set("entities", std::move(entities));
		structure.set("palette", std::move(palettes));
		return structure;
	}

	std::expected<Schematic, Error> readMcStructure(const std::span<const std::byte> data) {
		const auto root = nbt::read(data, nbt::Endian::Little);
		if (!root) {
			return std::unexpected(root.error());
		}

		return readStructure(root->compound);
	}

	std::vector<std::byte> writeMcStructure(const Schematic& schematic) {
		nbt::Compound root;
		root.set("format_version", kFormatVersion);
		root.set("size", positionList(schematic.size));
		root.set("structure", structureCompound(schematic));
		root.set("structure_world_origin", positionList(schematic.origin));
		return nbt::write(root, nbt::Endian::Little);
	}
}
