#include "java/blocks.h"

#include "bedschem/nbt/io.h"
#include "gzip.h"
#include "java/block_data.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace bedschem::java {
	namespace detail {
		struct property_info {
			std::string name;
			std::vector<std::string> values;
		};

		struct block_info {
			std::vector<property_info> properties;
			std::vector<std::size_t> defaults;
			int32_t first_state{};
		};

		struct string_hash {
			using is_transparent = void;

			[[nodiscard]] std::size_t operator()(const std::string_view value) const noexcept {
				return std::hash<std::string_view>{}(value);
			}
		};

		struct block_table {
			std::unordered_map<std::string, block_info, string_hash, std::equal_to<>> blocks;
			std::vector<BlockState> bedrock;
			std::vector<int32_t> mapping;
			int32_t version{};
		};

		[[nodiscard]] static std::vector<std::string> string_values(const nbt::List* list) {
			std::vector<std::string> values;
			if (!list) {
				return values;
			}

			for (const auto& item : list->items) {
				if (const auto* value = item.get<std::string>()) {
					values.push_back(*value);
				}
			}

			return values;
		}

		[[nodiscard]] static std::vector<std::size_t> default_values(const std::vector<property_info>& properties, int32_t default_offset) {
			std::vector<std::size_t> values(properties.size());
			for (auto index = properties.size(); index-- > 0;) {
				const auto count = static_cast<int32_t>(properties[index].values.size());
				values[index] = static_cast<std::size_t>(default_offset % count);
				default_offset /= count;
			}

			return values;
		}

		[[nodiscard]] static int32_t state_count(const std::vector<property_info>& properties) {
			int32_t count = 1;
			for (const auto& property : properties) {
				count *= static_cast<int32_t>(property.values.size());
			}

			return count;
		}

		static void read_blocks(block_table& table, const nbt::List& blocks) {
			int32_t next_state{};
			for (const auto& item : blocks.items) {
				const auto* block = item.get<nbt::Compound>();
				const auto* name = block ? block->get<std::string>("name") : nullptr;
				const auto* properties = block ? block->get<nbt::List>("properties") : nullptr;
				const auto* default_offset = block ? block->get<int32_t>("default") : nullptr;
				if (!name || !properties || !default_offset) {
					return;
				}

				block_info info{ .first_state = next_state };
				for (const auto& property_item : properties->items) {
					const auto* property = property_item.get<nbt::Compound>();
					const auto* property_name = property ? property->get<std::string>("name") : nullptr;
					if (!property_name) {
						return;
					}

					info.properties.push_back({ .name = *property_name, .values = string_values(property->get<nbt::List>("values")) });
				}

				info.defaults = default_values(info.properties, *default_offset);
				next_state += state_count(info.properties);
				table.blocks.emplace(*name, std::move(info));
			}
		}

		static void read_bedrock(block_table& table, const nbt::List& bedrock) {
			table.bedrock.reserve(bedrock.items.size());
			for (const auto& item : bedrock.items) {
				const auto* state = item.get<nbt::Compound>();
				const auto* name = state ? state->get<std::string>("name") : nullptr;
				const auto* states = state ? state->get<nbt::Compound>("states") : nullptr;
				table.bedrock.push_back({
					.name = name ? *name : std::string{},
					.states = states ? *states : nbt::Compound{},
					.version = table.version
				});
			}
		}

		[[nodiscard]] static block_table load_table() {
			block_table table;
			const auto data = blockData();
			const auto decompressed = decompressGzip(std::as_bytes(data));
			if (!decompressed) {
				return table;
			}

			const auto root = nbt::read(*decompressed, nbt::Endian::Little);
			if (!root) {
				return table;
			}

			const auto* version = root->compound.get<int32_t>("bedrock_version");
			const auto* blocks = root->compound.get<nbt::List>("blocks");
			const auto* bedrock = root->compound.get<nbt::List>("bedrock");
			const auto* mapping = root->compound.get<nbt::IntArray>("mapping");
			if (!version || !blocks || !bedrock || !mapping) {
				return table;
			}

			table.version = *version;
			read_bedrock(table, *bedrock);
			table.mapping = *mapping;
			read_blocks(table, *blocks);
			return table;
		}

		[[nodiscard]] static const block_table& table() {
			static const block_table instance = load_table();
			return instance;
		}

		[[nodiscard]] static std::optional<std::size_t> value_index(const property_info& property, std::span<const Property> properties) {
			const auto given = std::ranges::find(properties, property.name, &Property::name);
			if (given == properties.end()) {
				return std::nullopt;
			}

			const auto value = std::ranges::find(property.values, given->value);
			if (value == property.values.end()) {
				return std::nullopt;
			}

			return static_cast<std::size_t>(value - property.values.begin());
		}

		[[nodiscard]] static int32_t state_id(const block_info& block, std::span<const Property> properties) {
			int32_t offset{};
			for (std::size_t index = 0; index < block.properties.size(); ++index) {
				const auto& property = block.properties[index];
				const auto value = value_index(property, properties).value_or(block.defaults[index]);
				offset = offset * static_cast<int32_t>(property.values.size()) + static_cast<int32_t>(value);
			}

			return block.first_state + offset;
		}

		[[nodiscard]] static std::string namespaced(const std::string_view name) {
			if (name.find(':') != std::string_view::npos) {
				return std::string{ name };
			}

			return "minecraft:" + std::string{ name };
		}
	}

	static constexpr std::string_view kNamespace = "minecraft:";

	Conversion convert(const std::string_view name, const std::span<const Property> properties) {
		const auto waterlogged = std::ranges::any_of(properties, [](const Property& property) {
			return property.name == "waterlogged" && property.value == "true";
		});

		const auto& table = detail::table();
		const auto path = name.starts_with(kNamespace) ? name.substr(kNamespace.size()) : name;
		const auto vanilla = name.find(':') == std::string_view::npos || name.starts_with(kNamespace);
		const auto found = vanilla ? table.blocks.find(path) : table.blocks.end();
		const auto stateId = found != table.blocks.end() ? static_cast<std::size_t>(detail::state_id(found->second, properties)) : table.mapping.size();
		const auto bedrockIndex = stateId < table.mapping.size() ? static_cast<std::size_t>(table.mapping[stateId]) : table.bedrock.size();
		if (bedrockIndex >= table.bedrock.size()) {
			return {
				.block = { .name = detail::namespaced(name), .version = table.version },
				.waterlogged = waterlogged
			};
		}

		return {
			.block = table.bedrock[bedrockIndex],
			.waterlogged = waterlogged
		};
	}

	Conversion convert(const std::string_view blockState) {
		const auto open = blockState.find('[');
		const auto name = blockState.substr(0, open);
		std::vector<Property> properties;
		if (open == std::string_view::npos) {
			return convert(name, properties);
		}

		const auto close = blockState.rfind(']');
		auto remaining = blockState.substr(open + 1, close == std::string_view::npos || close < open ? std::string_view::npos : close - open - 1);
		while (!remaining.empty()) {
			const auto comma = remaining.find(',');
			const auto pair = remaining.substr(0, comma);
			const auto equals = pair.find('=');
			if (equals != std::string_view::npos) {
				properties.push_back({ .name = pair.substr(0, equals), .value = pair.substr(equals + 1) });
			}

			remaining = comma == std::string_view::npos ? std::string_view{} : remaining.substr(comma + 1);
		}

		return convert(name, properties);
	}

	Conversion convert(const nbt::Compound& entry) {
		const auto* name = entry.get<std::string>("Name");
		const auto* properties = entry.get<nbt::Compound>("Properties");
		std::vector<Property> converted;
		if (properties) {
			for (const auto& [key, tag] : *properties) {
				if (const auto* value = tag.get<std::string>()) {
					converted.push_back({ .name = key, .value = *value });
				}
			}
		}

		return convert(name ? std::string_view{ *name } : std::string_view{ "minecraft:air" }, converted);
	}

	[[nodiscard]] static int32_t paletteIndex(std::vector<BlockState>& palette, const BlockState& state) {
		const auto found = std::ranges::find(palette, state);
		if (found != palette.end()) {
			return static_cast<int32_t>(found - palette.begin());
		}

		palette.push_back(state);
		return static_cast<int32_t>(palette.size() - 1);
	}

	PaletteEntry addToPalette(std::vector<BlockState>& palette, const Conversion& conversion) {
		const auto block = paletteIndex(palette, conversion.block);
		if (!conversion.waterlogged) {
			return { .block = block };
		}

		const auto water = convert("minecraft:water", {});
		return {
			.block = block,
			.liquid = paletteIndex(palette, water.block)
		};
	}

	void place(Schematic& schematic, const BlockPos position, const PaletteEntry entry) {
		schematic.setBlock(0, position, entry.block);
		schematic.setBlock(1, position, entry.liquid);
	}
}
