import argparse
import gzip
import json
import pathlib
import struct

END, BYTE, SHORT, INT, LONG, FLOAT, DOUBLE, BYTE_ARRAY, STRING, LIST, COMPOUND, INT_ARRAY, LONG_ARRAY = range(13)

class Reader:
    def __init__(self, data, endian):
        self.data = data
        self.offset = 0
        self.endian = endian

    def unpack(self, fmt):
        size = struct.calcsize(fmt)
        value = struct.unpack_from(self.endian + fmt, self.data, self.offset)[0]
        self.offset += size
        return value

    def string(self):
        length = self.unpack("H")
        value = self.data[self.offset:self.offset + length].decode("utf-8")
        self.offset += length
        return value

    def payload(self, tag_type):
        if tag_type == BYTE:
            return self.unpack("b")
        if tag_type == SHORT:
            return self.unpack("h")
        if tag_type == INT:
            return self.unpack("i")
        if tag_type == LONG:
            return self.unpack("q")
        if tag_type == FLOAT:
            return self.unpack("f")
        if tag_type == DOUBLE:
            return self.unpack("d")
        if tag_type == BYTE_ARRAY:
            length = self.unpack("i")
            value = self.data[self.offset:self.offset + length]
            self.offset += length
            return value
        if tag_type == STRING:
            return self.string()
        if tag_type == LIST:
            item_type = self.unpack("b")
            count = self.unpack("i")
            return item_type, [self.payload(item_type) for _ in range(count)]
        if tag_type == COMPOUND:
            entries = []
            while True:
                entry_type = self.unpack("b")
                if entry_type == END:
                    return entries
                name = self.string()
                entries.append((name, entry_type, self.payload(entry_type)))
        if tag_type == INT_ARRAY:
            return [self.unpack("i") for _ in range(self.unpack("i"))]
        if tag_type == LONG_ARRAY:
            return [self.unpack("q") for _ in range(self.unpack("i"))]
        raise ValueError(f"unsupported tag type {tag_type}")

    def root(self):
        tag_type = self.unpack("b")
        self.string()
        return self.payload(tag_type)


class Writer:
    def __init__(self):
        self.parts = []

    def pack(self, fmt, value):
        self.parts.append(struct.pack("<" + fmt, value))

    def string(self, value):
        encoded = value.encode("utf-8")
        self.pack("H", len(encoded))
        self.parts.append(encoded)

    def payload(self, tag_type, value):
        if tag_type == BYTE:
            self.pack("b", value)
        elif tag_type == SHORT:
            self.pack("h", value)
        elif tag_type == INT:
            self.pack("i", value)
        elif tag_type == STRING:
            self.string(value)
        elif tag_type == LIST:
            item_type, items = value
            self.pack("b", item_type if items else END)
            self.pack("i", len(items))
            for item in items:
                self.payload(item_type, item)
        elif tag_type == COMPOUND:
            for name, entry_type, entry in value:
                self.pack("b", entry_type)
                self.string(name)
                self.payload(entry_type, entry)
            self.pack("b", END)
        elif tag_type == INT_ARRAY:
            self.pack("i", len(value))
            for item in value:
                self.pack("i", item)
        else:
            raise ValueError(f"unsupported tag type {tag_type}")

    def root(self, value):
        self.pack("b", COMPOUND)
        self.string("")
        self.payload(COMPOUND, value)
        return b"".join(self.parts)


def field(compound, name):
    return next((value for key, _, value in compound if key == name), None)


def java_blocks(path):
    blocks = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    blocks.sort(key=lambda block: block["minStateId"])
    expected = 0
    entries = []
    for block in blocks:
        if block["minStateId"] != expected:
            raise ValueError(f"state ids are not contiguous at {block['name']}")

        properties = []
        count = 1
        for state in block["states"]:
            values = state.get("values") or ["true", "false"]
            if len(values) != state["num_values"]:
                raise ValueError(f"{block['name']}.{state['name']} has {len(values)} values, expected {state['num_values']}")

            properties.append([("name", STRING, state["name"]), ("values", LIST, (STRING, values))])
            count *= len(values)

        if block["maxStateId"] - block["minStateId"] + 1 != count:
            raise ValueError(f"{block['name']} has {count} states, expected {block['maxStateId'] - block['minStateId'] + 1}")

        entries.append([
            ("name", STRING, block["name"]),
            ("properties", LIST, (COMPOUND, properties)),
            ("default", INT, block["defaultState"] - block["minStateId"])
        ])
        expected += count

    return entries, expected


def bedrock_states(path, java_names):
    raw = pathlib.Path(path).read_bytes()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)

    mappings = field(Reader(raw, ">").root(), "bedrock_mappings")[1]
    if len(mappings) != len(java_names):
        raise ValueError(f"Geyser maps {len(mappings)} states, the Java table has {len(java_names)}")

    palette = []
    lookup = {}
    indices = []
    for java_name, entry in zip(java_names, mappings):
        identifier = field(entry, "bedrock_identifier") or java_name
        states = field(entry, "state") or []
        name = identifier if ":" in identifier else "minecraft:" + identifier
        key = (name, repr(states))
        if key not in lookup:
            lookup[key] = len(palette)
            palette.append([("name", STRING, name), ("states", COMPOUND, states)])

        indices.append(lookup[key])

    return palette, indices


def main():
    parser = argparse.ArgumentParser(description="Builds the embedded Java to Bedrock block table from GeyserMC mappings.")
    parser.add_argument("geyser_blocks", help="blocks.nbt from GeyserMC/mappings")
    parser.add_argument("java_blocks", help="blocks.json from PrismarineJS/minecraft-data for the same Java version")
    parser.add_argument("--java-version", required=True)
    parser.add_argument("--bedrock-version", type=int, required=True, help="block version written to Bedrock palettes")
    parser.add_argument("--output", default=str(pathlib.Path(__file__).resolve().parent.parent / "src" / "java" / "block_data.nbt"))
    arguments = parser.parse_args()

    blocks, state_count = java_blocks(arguments.java_blocks)
    java_names = []
    for block in blocks:
        name = field(block, "name")
        count = 1
        for property_entry in field(block, "properties")[1]:
            count *= len(field(property_entry, "values")[1])
        java_names.extend([name] * count)

    palette, indices = bedrock_states(arguments.geyser_blocks, java_names)
    root = [
        ("java_version", STRING, arguments.java_version),
        ("bedrock_version", INT, arguments.bedrock_version),
        ("blocks", LIST, (COMPOUND, blocks)),
        ("bedrock", LIST, (COMPOUND, palette)),
        ("mapping", INT_ARRAY, indices)
    ]
    data = gzip.compress(Writer().root(root), compresslevel=9, mtime=0)
    pathlib.Path(arguments.output).write_bytes(data)
    print(f"{len(blocks)} blocks, {state_count} Java states, {len(palette)} Bedrock states, {len(data)} bytes")


if __name__ == "__main__":
    main()
