# Content keys and type descriptors

This folder holds the keys of the content compiler. A key is 128 bits of XXH3 over a canonical encoding of a recipe.
A harmless edit that changes a key costs a cache miss. A meaningful edit that keeps a key gives wrong content. When a
choice is in doubt, the encoding chooses the miss.

## Files

| File | Holds |
|---|---|
| `reflect/Describe.hpp` | `Describe<T>`, `Field`, `EnumCase`, and the concepts `DescribedStruct` and `DescribedEnum`. |
| `Key.hpp` | `Key<Tag>`, the aliases `RecipeKey`, `BlobKey`, `ArtifactHash`, the map hasher and the hex text. |
| `CanonicalSink.hpp` | The abstract sink and the one-byte tags. |
| `HashSink.hpp` | The sink that feeds XXH3-128 in streaming mode. |
| `TextSink.hpp` | The sink that prints the same walk as indented text. |
| `Encode.hpp` | `Encode` (the one template that walks a value) and `FindNaN`. |
| `RecipeKey.hpp` | `MakeRecipeKey`, `HashBlob`, `RecipeInput`, `KeyError`. |

## How to describe a type

Write the descriptor by hand, in `foundation::reflect`. A struct has a name and a tuple of fields. An enum has a name
and its cases.

```cpp
template<>
struct foundation::reflect::Describe<NoiseParams>
{
    static constexpr std::string_view Name = "NoiseParams";
    static constexpr std::tuple Fields
    {
        Field<NoiseParams, float>{ "Amplitude", &NoiseParams::Amplitude },
        Field<NoiseParams, uint32_t>{ "Seed", &NoiseParams::Seed },
    };
};
```

Rules for the author of a descriptor:

- List every field that changes the result. A field that is not listed is not in the key.
- The order of the list is the order of the encoding. Changing the order changes the key.
- Renaming a field or a type changes the key. Renaming is a deliberate cache miss.
- C++26 reflection will replace the hand-written bodies. Nothing downstream changes.

## What the encoding writes

Every write starts with a one-byte tag (`CanonicalTag`). All integers are little-endian.

| Value | Bytes after the tag |
|---|---|
| `bool` | one byte, 0 or 1 |
| integer | width in bytes, signedness byte, then the value at its own width |
| `float`, `double` | the exact bits |
| string | length as `uint64_t`, then the characters |
| enum | type name and case name, each as length and characters |
| struct | type name; per field a `Field` tag and the field name, then the value; an `EndStruct` tag |
| `vector`, `array`, `span` | count as `uint64_t`, the elements, an `EndArray` tag |
| `optional` | one presence byte, then the value when present |
| `variant` | the alternative's `Describe` name, then the value |
| key | `Low` then `High` as two `uint64_t` |

The consequences:

- `int32_t 1` and `int64_t 1` give different bytes, because the width is in the encoding.
- `-0.0f` and `+0.0f` give different bytes, because floats are exact bits. NaN has many bit patterns, so
  `MakeRecipeKey` refuses it and returns `KeyErrorCode::NaNParameter` with the dotted path of the field.
- An enum is written by case name. Inserting a case before another case does not change the key of the other case.
- Padding bytes are never read. The walk reads each member, so garbage in padding cannot change a key.
- `vector`, `array` and `span` of the same elements give the same bytes. The container type is not part of the key.
- A file is never a parameter. A source file is an input, keyed by `HashBlob` of its bytes.

## What a recipe key hashes, in order

1. The domain `"dd.recipe"` and the encoding version (`k_EncodingVersion`).
2. The kind name and the kind version.
3. The parameters, through `Encode`.
4. The input count, then for each input the slot name, the alternative name (`RecipeKey` or `BlobKey`) and the key.

`HashBlob` hashes the domain `"dd.blob"`, the encoding version, the byte count, then the bytes. A blob key and a recipe
key can never be equal by accident of content, and the two types do not convert into each other.

When the encoding changes on purpose, raise `k_EncodingVersion`. The known-answer tests in `test_keys.cpp` fail first,
and that is their job.

## Why a key changed

Encode the old and the new parameters with `TextSink` and diff the two texts. Floats show as a decimal value and as
their hex bits, so a one-bit change is visible.

```cpp
foundation::content::TextSink sink;
foundation::content::Encode(params, sink);
std::println("{}", sink.Text());
```

## Notes on the implementation

- `HashSink` keeps the XXH3 state in a fixed, aligned buffer inside the object. `xxhash.h` is included only in
  `HashSink.cpp`, and `foundation` links xxHash PRIVATE. A key costs no allocation.
- `CanonicalSink` is virtual, so the encoder is one template and the sinks are ordinary classes in source files.
- `FindNaN` is a separate walk over the same descriptors. It runs before the hash, so a refused parameter never
  produces a key.
