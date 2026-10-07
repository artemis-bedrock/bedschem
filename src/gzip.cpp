#include "gzip.h"

#include "failure.h"

#include <algorithm>
#include <memory>
#include <libdeflate.h>

namespace bedschem {
	static constexpr std::size_t kMaximumSize = std::size_t{ 1 } << 30;
	static constexpr std::size_t kMinimumSize = std::size_t{ 1 } << 16;
	static constexpr std::size_t kTrailerSize = 4;

	[[nodiscard]] static bool isGzip(const std::span<const std::byte> data) {
		return data.size() >= 2 && data[0] == std::byte{ 0x1F } && data[1] == std::byte{ 0x8B };
	}

	[[nodiscard]] static std::size_t declaredSize(const std::span<const std::byte> data) {
		const auto trailer = data.last(kTrailerSize);
		uint32_t size{};
		for (std::size_t i = 0; i < kTrailerSize; ++i) {
			size |= std::to_integer<uint32_t>(trailer[i]) << (8 * i);
		}

		return size;
	}

	std::expected<std::vector<std::byte>, Error> decompressGzip(const std::span<const std::byte> data) {
		if (!isGzip(data)) {
			return std::vector<std::byte>{ data.begin(), data.end() };
		}

		const std::unique_ptr<libdeflate_decompressor, decltype(&libdeflate_free_decompressor)> decompressor{ libdeflate_alloc_decompressor(), &libdeflate_free_decompressor };
		if (!decompressor) {
			return failure(ErrorCode::InvalidCompression, "gzip");
		}

		std::vector<std::byte> output(std::clamp(declaredSize(data), kMinimumSize, kMaximumSize));
		while (true) {
			std::size_t written{};
			const auto result = libdeflate_gzip_decompress(decompressor.get(), data.data(), data.size(), output.data(), output.size(), &written);
			if (result == LIBDEFLATE_SUCCESS) {
				output.resize(written);
				return output;
			}

			if (result != LIBDEFLATE_INSUFFICIENT_SPACE || output.size() >= kMaximumSize) {
				return failure(ErrorCode::InvalidCompression, "gzip");
			}

			output.resize(std::min(output.size() * 2, kMaximumSize));
		}
	}
}
