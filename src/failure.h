#pragma once

#include "bedschem/error.h"

#include <expected>
#include <string>
#include <utility>

namespace bedschem {
	[[nodiscard]] inline std::unexpected<Error> failure(const ErrorCode code, std::string context) {
		return std::unexpected(Error{ .code = code, .context = std::move(context) });
	}
}
