#include "java/block_data.h"

namespace bedschem::java {
	static constexpr unsigned char kBlockData[]{
#embed "block_data.nbt"
	};

	std::span<const unsigned char> blockData() {
		return kBlockData;
	}
}
