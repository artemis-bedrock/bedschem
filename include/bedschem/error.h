#pragma once

#include <cstdint>
#include <string>

namespace bedschem {
	enum class ErrorCode : uint8_t {
		UnexpectedEnd,
		InvalidTagType,
		InvalidLength,
		DepthExceeded,
		RootNotCompound,
		MissingField,
		InvalidField
	};

	struct Error {
		ErrorCode code{};
		std::string context;
	};
}
