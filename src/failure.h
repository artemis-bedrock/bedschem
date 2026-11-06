#pragma once

#include "bedschem/error.h"

#include <nbt.h>
#include <expected>
#include <string>
#include <utility>

namespace bedschem {
	[[nodiscard]] inline std::unexpected<Error> failure(const ErrorCode code, std::string context) {
		return std::unexpected(Error{ .code = code, .context = std::move(context) });
	}

	[[nodiscard]] inline ErrorCode nbtErrorCode(const nbt::ErrorCode code) {
		switch (code) {
			case nbt::ErrorCode::UnexpectedEnd:
				return ErrorCode::UnexpectedEnd;
			case nbt::ErrorCode::InvalidTagType:
				return ErrorCode::InvalidTagType;
			case nbt::ErrorCode::InvalidLength:
				return ErrorCode::InvalidLength;
			case nbt::ErrorCode::DepthExceeded:
				return ErrorCode::DepthExceeded;
			case nbt::ErrorCode::RootNotCompound:
				return ErrorCode::RootNotCompound;
		}

		return ErrorCode::InvalidField;
	}

	[[nodiscard]] inline Error nbtError(const nbt::Error& error) {
		return Error{
			.code = nbtErrorCode(error.code),
			.context = "offset " + std::to_string(error.offset)
		};
	}
}
