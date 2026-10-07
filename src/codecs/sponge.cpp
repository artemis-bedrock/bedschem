#include "codecs/sponge.h"

#include "failure.h"
#include "java/blocks.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace bedschem::codecs::sponge {
	static constexpr int32_t kLatestVersion = 3;
	static constexpr uint32_t kVarIntPayload = 0x7F;
	static constexpr uint32_t kVarIntContinue = 0x80;
	static constexpr int32_t kVarIntMaximumShift = 28;

	struct Layout {
		const nbt::Compound* palette{};
		const nbt::ByteArray* data{};
	};

	[[nodiscard]] static std::optional<int32_t> readDimension(const nbt::Compound& schematic, const std::string_view name) {
		const auto* value = schematic.get<int16_t>(name);
		if (!value) {
			return std::nullopt;
		}

		return static_cast<uint16_t>(*value);
	}

	[[nodiscard]] static std::expected<Layout, Error> readLayout(const nbt::Compound& schematic, const int32_t version) {
		if (version < 3) {
			return Layout{
				.palette = schematic.get<nbt::Compound>("Palette"),
				.data = schematic.get<nbt::ByteArray>("BlockData")
			};
		}

		const auto* blocks = schematic.get<nbt::Compound>("Blocks");
		if (!blocks) {
			return failure(ErrorCode::MissingField, "Blocks");
		}

		return Layout{
			.palette = blocks->get<nbt::Compound>("Palette"),
			.data = blocks->get<nbt::ByteArray>("Data")
		};
	}

	[[nodiscard]] static std::expected<std::vector<java::PaletteEntry>, Error> readPalette(const nbt::Compound& entries, std::vector<BlockState>& palette) {
		std::vector<java::PaletteEntry> converted;
		for (const auto& [blockState, tag] : entries) {
			const auto* id = tag.get<int32_t>();
			if (!id || *id < 0) {
				return failure(ErrorCode::InvalidField, "Palette." + blockState);
			}

			const auto index = static_cast<std::size_t>(*id);
			if (index >= converted.size()) {
				converted.resize(index + 1);
			}

			converted[index] = java::addToPalette(palette, java::convert(blockState));
		}

		return converted;
	}

	[[nodiscard]] static std::optional<uint32_t> readVarInt(const nbt::ByteArray& data, std::size_t& offset) {
		uint32_t value{};
		for (int32_t shift = 0; shift <= kVarIntMaximumShift; shift += 7) {
			if (offset >= data.size()) {
				return std::nullopt;
			}

			const auto byte = static_cast<uint32_t>(static_cast<uint8_t>(data[offset++]));
			value |= (byte & kVarIntPayload) << shift;
			if ((byte & kVarIntContinue) == 0) {
				return value;
			}
		}

		return std::nullopt;
	}

	[[nodiscard]] static std::expected<Schematic, Error> readBlocks(const nbt::Compound& schematic) {
		const auto* version = schematic.get<int32_t>("Version");
		if (!version) {
			return failure(ErrorCode::MissingField, "Version");
		}

		if (*version < 1 || *version > kLatestVersion) {
			return failure(ErrorCode::UnsupportedVersion, "Version " + std::to_string(*version));
		}

		const auto width = readDimension(schematic, "Width");
		const auto height = readDimension(schematic, "Height");
		const auto length = readDimension(schematic, "Length");
		if (!width || !height || !length) {
			return failure(ErrorCode::MissingField, "Width, Height or Length");
		}

		const BlockPos size{ .x = *width, .y = *height, .z = *length };
		const auto volume = static_cast<int64_t>(size.x) * size.y * size.z;
		if (volume <= 0 || volume > std::numeric_limits<int32_t>::max()) {
			return failure(ErrorCode::InvalidField, "Width, Height or Length");
		}

		const auto layout = readLayout(schematic, *version);
		if (!layout) {
			return std::unexpected(layout.error());
		}

		if (!layout->palette || !layout->data) {
			return failure(ErrorCode::MissingField, "Palette or block data");
		}

		auto result = Schematic::filled(size);
		const auto entries = readPalette(*layout->palette, result.palette);
		if (!entries) {
			return std::unexpected(entries.error());
		}

		std::size_t offset{};
		const auto layerSize = static_cast<int64_t>(size.x) * size.z;
		for (int64_t index = 0; index < volume; ++index) {
			const auto value = readVarInt(*layout->data, offset);
			if (!value) {
				return failure(ErrorCode::InvalidLength, "block data");
			}

			if (*value >= entries->size()) {
				return failure(ErrorCode::InvalidField, "block data[" + std::to_string(index) + "]");
			}

			const BlockPos position{
				.x = static_cast<int32_t>(index % size.x),
				.y = static_cast<int32_t>(index / layerSize),
				.z = static_cast<int32_t>(index % layerSize / size.x)
			};
			java::place(result, position, (*entries)[*value]);
		}

		return result;
	}

	[[nodiscard]] static const nbt::Compound& schematicOf(const nbt::Compound& root) {
		const auto* nested = root.get<nbt::Compound>("Schematic");
		return nested ? *nested : root;
	}

	bool matches(const nbt::Compound& root) {
		const auto& schematic = schematicOf(root);
		return schematic.contains("Version") && schematic.contains("Width") && (schematic.contains("Palette") || schematic.contains("Blocks"));
	}

	std::expected<Schematic, Error> read(const nbt::Compound& root) {
		return readBlocks(schematicOf(root));
	}
}
