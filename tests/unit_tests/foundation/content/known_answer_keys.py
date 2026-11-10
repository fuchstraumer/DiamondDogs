"""Independent encoder for the known-answer keys in test_keys.cpp and test_bytes_sink.cpp.

It is written from the description of the stream in foundation/include/content/README.md, and not translated from
the C++. It needs the xxhash module (xxh3_128). Run it with no argument for the current encoding. Run it with
--old for encoding version 1 (an input has no output name), which must print the keys that the tests held before
version 2.
"""

import struct
import sys

import xxhash

TAG_BOOL = 1
TAG_INT = 2
TAG_FLOAT32 = 3
TAG_FLOAT64 = 4
TAG_STRING = 5
TAG_ENUM = 6
TAG_STRUCT = 7
TAG_END_STRUCT = 8
TAG_FIELD = 9
TAG_ARRAY = 10
TAG_END_ARRAY = 11
TAG_OPTIONAL = 12
TAG_VARIANT = 13
TAG_KEY = 14


def raw_string(text):
    data = text.encode("utf-8")
    return struct.pack("<Q", len(data)) + data


def string(text):
    return bytes([TAG_STRING]) + raw_string(text)


def integer(value, width, is_signed):
    fmt = {1: "b", 2: "h", 4: "i", 8: "q"}[width] if is_signed else {1: "B", 2: "H", 4: "I", 8: "Q"}[width]
    return bytes([TAG_INT, width, 1 if is_signed else 0]) + struct.pack("<" + fmt, value)


def float32(value):
    return bytes([TAG_FLOAT32]) + struct.pack("<f", value)


def float64(value):
    return bytes([TAG_FLOAT64]) + struct.pack("<d", value)


def boolean(value):
    return bytes([TAG_BOOL, 1 if value else 0])


def enum(type_name, case_name):
    return bytes([TAG_ENUM]) + raw_string(type_name) + raw_string(case_name)


def field(name):
    return bytes([TAG_FIELD]) + raw_string(name)


def struct_begin(name):
    return bytes([TAG_STRUCT]) + raw_string(name)


def struct_end():
    return bytes([TAG_END_STRUCT])


def array(elements):
    return bytes([TAG_ARRAY]) + struct.pack("<Q", len(elements)) + b"".join(elements) + bytes([TAG_END_ARRAY])


def optional(encoded_or_none):
    if encoded_or_none is None:
        return bytes([TAG_OPTIONAL, 0])
    return bytes([TAG_OPTIONAL, 1]) + encoded_or_none


def key_bytes(low, high):
    return bytes([TAG_KEY]) + struct.pack("<QQ", low, high)


def noise_params(wave_case, amplitude, seed):
    return (struct_begin("NoiseParams")
            + field("Kind") + enum("WaveKind", wave_case)
            + field("Amplitude") + float32(amplitude)
            + field("Seed") + integer(seed, 4, False)
            + struct_end())


def tile_params(weights, limit, label):
    limit_encoded = None if limit is None else integer(limit, 4, True)
    return (struct_begin("TileParams")
            + field("Weights") + array([float32(weight) for weight in weights])
            + field("Limit") + optional(limit_encoded)
            + field("Label") + string(label)
            + struct_end())


def planet_params():
    return (struct_begin("PlanetParams")
            + field("Noise") + noise_params("Sine", -0.5, 0xDEADBEEF)
            + field("Tile") + tile_params([], None, "")
            + field("Radius") + float64(6371.0)
            + field("Wrap") + boolean(True)
            + struct_end())


def recipe_input(version, slot, source_kind, low, high, output):
    encoded = string(slot) + bytes([TAG_VARIANT]) + raw_string(source_kind) + key_bytes(low, high)

    if version >= 2 and source_kind == "RecipeKey":
        encoded += string(output)

    return encoded


def recipe_key(version, kind, kind_version, params_bytes, inputs):
    stream = (string("dd.recipe") + integer(version, 4, False) + string(kind) + integer(kind_version, 4, False)
              + params_bytes + array(inputs))
    digest = xxhash.xxh3_128_intdigest(stream)

    return format(digest, "032x")


def main():
    version = 1 if "--old" in sys.argv else 2

    noise = recipe_key(version, "noise", 1, noise_params("Square", 1.5, 42), [])
    tile = recipe_key(version, "tile", 3, tile_params([0.25, 2.0], 7, "grass"), [])
    nested = recipe_key(version, "planet", 2, planet_params(), [
        recipe_input(version, "heights", "RecipeKey", 1, 2, "Value"),
        recipe_input(version, "mask", "BlobKey", 3, 4, ""),
    ])

    print("KnownAnswerNoiseParams", noise)
    print("KnownAnswerVectorAndOptional", tile)
    print("KnownAnswerNestedWithInputs", nested)

    if version >= 2:
        with_outputs = recipe_key(version, "noise", 1, noise_params("Square", 1.5, 42), [
            recipe_input(version, "source", "RecipeKey", 5, 6, "Height"),
            recipe_input(version, "blob", "BlobKey", 7, 8, ""),
        ])
        print("KnownAnswerInputsWithOutputs", with_outputs)


main()
