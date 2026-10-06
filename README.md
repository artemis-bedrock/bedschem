# bedschem

Schematic parsing and exporting tools for Minecraft Bedrock

## Basic Usage

```cpp
const auto schematic = bedschem::readMcStructure(bytes);
if (!schematic) {
    return schematic.error();
}

const auto rotated = bedschem::rotate(*schematic, bedschem::Rotation::Clockwise90);
const auto blockIndex = rotated.block(0, { 1, 0, 2 });
const auto out = bedschem::writeMcStructure(rotated);
```