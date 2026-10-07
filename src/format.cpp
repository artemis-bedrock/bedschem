#include "bedschem/format.h"

#include "bedschem/nbt/io.h"
#include "failure.h"
#include "codecs/litematic.h"
#include "codecs/mcstructure.h"
#include "codecs/sponge.h"
#include "codecs/structure.h"
#include "gzip.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace bedschem {
	struct Codec {
		FormatInfo info;
		nbt::Endian endian{};
		bool (*matches)(const nbt::Compound& root){};
		std::expected<Schematic, Error> (*read)(const nbt::Compound& root){};
		nbt::Compound (*write)(const Schematic& schematic){};
	};

	static constexpr std::array kCodecs{
		Codec{
			.info = { .format = Format::McStructure, .name = "Bedrock structure", .extension = ".mcstructure", .writable = true },
			.endian = nbt::Endian::Little,
			.matches = &codecs::mcstructure::matches,
			.read = &codecs::mcstructure::read,
			.write = &codecs::mcstructure::write
		},
		Codec{
			.info = { .format = Format::Litematic, .name = "Litematic", .extension = ".litematic" },
			.endian = nbt::Endian::Big,
			.matches = &codecs::litematic::matches,
			.read = &codecs::litematic::read
		},
		Codec{
			.info = { .format = Format::Sponge, .name = "Sponge schematic", .extension = ".schem" },
			.endian = nbt::Endian::Big,
			.matches = &codecs::sponge::matches,
			.read = &codecs::sponge::read
		},
		Codec{
			.info = { .format = Format::JavaStructure, .name = "Java structure", .extension = ".nbt" },
			.endian = nbt::Endian::Big,
			.matches = &codecs::structure::matches,
			.read = &codecs::structure::read
		}
	};

	static constexpr auto kFormats = [] {
		std::array<FormatInfo, kCodecs.size()> infos{};
		std::ranges::transform(kCodecs, infos.begin(), &Codec::info);
		return infos;
	}();

	[[nodiscard]] static const Codec& codecFor(const Format format) {
		return *std::ranges::find(kCodecs, format, [](const Codec& codec) {
			return codec.info.format;
		});
	}

	[[nodiscard]] static bool equalsIgnoringCase(const std::string_view left, const std::string_view right) {
		return std::ranges::equal(left, right, [](const unsigned char a, const unsigned char b) {
			return std::tolower(a) == std::tolower(b);
		});
	}

	std::span<const FormatInfo> formats() {
		return kFormats;
	}

	const FormatInfo& formatInfo(const Format format) {
		return codecFor(format).info;
	}

	std::optional<Format> formatForExtension(std::string_view extension) {
		if (extension.starts_with('.')) {
			extension.remove_prefix(1);
		}

		const auto found = std::ranges::find_if(kFormats, [&](const FormatInfo& info) {
			return equalsIgnoringCase(info.extension.substr(1), extension);
		});

		if (found == kFormats.end()) {
			return std::nullopt;
		}

		return found->format;
	}

	std::expected<Schematic, Error> read(const std::span<const std::byte> data) {
		const auto decompressed = decompressGzip(data);
		if (!decompressed) {
			return std::unexpected(decompressed.error());
		}

		std::array<std::optional<std::expected<nbt::NamedCompound, Error>>, 2> roots;
		for (const auto& codec : kCodecs) {
			auto& root = roots[static_cast<std::size_t>(codec.endian)];
			if (!root) {
				root = nbt::read(*decompressed, codec.endian);
			}

			if (*root && codec.matches((*root)->compound)) {
				return codec.read((*root)->compound);
			}
		}

		return failure(ErrorCode::UnsupportedFormat, "unrecognised schematic");
	}

	std::expected<Schematic, Error> read(const std::span<const std::byte> data, const Format format) {
		const auto decompressed = decompressGzip(data);
		if (!decompressed) {
			return std::unexpected(decompressed.error());
		}

		const auto& codec = codecFor(format);
		const auto root = nbt::read(*decompressed, codec.endian);
		if (!root) {
			return std::unexpected(root.error());
		}

		if (!codec.matches(root->compound)) {
			return failure(ErrorCode::UnsupportedFormat, std::string{ codec.info.name });
		}

		return codec.read(root->compound);
	}

	std::expected<std::vector<std::byte>, Error> write(const Schematic& schematic, const Format format) {
		const auto& codec = codecFor(format);
		if (!codec.write) {
			return failure(ErrorCode::UnsupportedFormat, std::string{ codec.info.name } + " is read only");
		}

		return nbt::write(codec.write(schematic), codec.endian);
	}
}
