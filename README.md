# bedschem

Schematic parsing and exporting tools for Minecraft Bedrock

## Basic Usage

```cpp
const auto schematic = bedschem::read(bytes);
if (!schematic) {
    return schematic.error();
}

const auto rotated = bedschem::rotate(*schematic, bedschem::Rotation::Clockwise90);
const auto blockIndex = rotated.block(0, { 1, 0, 2 });
const auto out = bedschem::write(rotated, bedschem::Format::McStructure);
```

## Formats

| Format | Extension | Read | Write |
| --- | --- | --- | --- |
| `Format::McStructure` | `.mcstructure` | yes | yes |
| `Format::Litematic` | `.litematic` | yes | |
| `Format::Sponge` (versions 1 to 3) | `.schem` | yes | |

Java schematics are converted to Bedrock block states while they are read. 

## Java block table

The Java to Bedrock block table (`src/java/block_data.nbt`) is built by `tools/generate_java_blocks.py` from [GeyserMC/mappings](https://github.com/GeyserMC/mappings) and [PrismarineJS/minecraft-data](https://github.com/PrismarineJS/minecraft-data). It currently covers Java 26.2 and Bedrock 1.26.50.

The block mappings are derived from GeyserMC/mappings, Copyright (c) 2019-2026 GeyserMC, licensed under the MIT license.
