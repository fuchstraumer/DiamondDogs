# Reflection

This folder describes authored types. `Describe<T>` is the one list of a type's fields. Every other use reads that
list, so a type never needs a second registration.

Reflection covers authored types only. Runtime structs need no descriptor, with one exception: Loom params blocks.

## Files

| File | Holds |
|---|---|
| `Describe.hpp` | `Describe<T>`, `Field`, `EnumCase`, `FieldMetadata`, `FieldRange`, and the compile-time descriptor checks. |
| `ValueTraits.hpp` | The value types that a descriptor may contain. `Encode` and `TypeInfo` share them. |
| `TypeInfo.hpp` | `GetTypeInfo<T>()`, the type-erased table, and the field and enum case lookups. |
| `ValueAccess.hpp` | `ResolvePath` and `SetValue`: read and write a value by a path, with no compile-time type. |

## How to describe a type

`foundation/include/content/README.md` has the rules for fields that feed a recipe key. Metadata is optional, and it
goes after the member pointer:

```cpp
template<>
struct foundation::reflect::Describe<SeaState>
{
    static constexpr std::string_view Name = "ocean.SeaState";
    static constexpr std::string_view Doc = "The sea that the simulation builds its spectrum from.";
    static constexpr std::tuple Fields
    {
        Field<SeaState, float>{ "Depth", &SeaState::Depth, { .Unit = "m", .Range = FieldRange{ 1.0, 1000.0 } } },
        Field<SeaState, float>{ "Spread", &SeaState::Spread },
    };
};
```

Put a descriptor in its own `<Type>.reflect.hpp`, beside the header of the type, and include it only in the files that
use the table. The header of the type stays clean, and a change to a descriptor recompiles only those files. A file that
uses the table and does not include the descriptor fails to compile. Define the descriptor before the first use of the
type's table in a file: a specialization that comes after that use makes the program ill-formed, and the compiler does
not have to report it.

Recipe keys ignore metadata, the type doc and enum case docs. Editing them never invalidates content.

| Metadata | Meaning |
|---|---|
| `Unit` | The unit of the value, as text. |
| `Range` | The inclusive limits that a write must respect. Leave it empty when the field has no known limit. A UI slider range is not automatically this limit: sliders are often narrower than the legal values. |
| `Doc` | One or two sentences for a reader who sees only this field. |
| `ReadOnly` | Tools may read this field and must not write it. |
| `Hidden` | Tools leave this field out of listings. Use it for large or noisy data. |
| `Required` | A command call must give this field. Use it on the top-level fields of a command's `Params`. |

`CheckStructDescriptor` and `CheckEnumDescriptor` fail the build for a duplicate field name, a duplicate case name, or
a range with `Min > Max` or a NaN limit. `Encode` and `GetTypeInfo` both call them.

## TypeInfo

`GetTypeInfo<T>()` returns a table that the compiler builds. There is no registration step, no static initializer and
no allocation. Any thread can read a table at any time, including before `main`. A table exists only when code asks
for it, so describing a type costs nothing until something uses it.

- `TypeKind` has the same value types as `Encode`, and the two classify in the same order.
- `FieldInfo::Address` returns the address of a field from a pointer to its owner. `FieldAddress` is the const form.
- `FieldInfo::Type`, `TypeInfo::Element` and `TypeInfo::Alternatives` are functions. A pointer to a table cannot form a
  cycle at compile time, and a function can, so a type may contain a vector of itself.
- `EnumCaseInfo::Value` is sign-extended or zero-extended to 64 bits, as the integer encoding does.
- Do not compare the addresses of two tables. The linker may fold tables with the same contents.

`GetTypeInfo` is stricter than `Encode` in one case: it refuses `vector<bool>`, because the elements have no addresses.
Use `vector<uint8_t>`.

## Reading through the table

`content::EncodeErased(type, address, sink)` is the type-erased form of `Encode`. It writes to the same sinks, and its
output is byte-identical to `Encode<T>`. `test_encode_erased.cpp` compares the two for every kind of value. To read one
value by path, call `ResolvePath`, and then pass its `Type` and `Address` to `EncodeErased`.

The two differ on an enum value that has no case. Both write `<no case>`, but `Encode` asserts first and `EncodeErased`
does not. A Debug assert opens a dialog, and a tool that reads live data must not block on one.

## Paths

A path is a list of field names and indices: `Spectrum.Bands[2].Height`, `Grid[1][0]`. An empty path is the root.

- Field names match exactly. Case-insensitive matching belongs to the caller.
- Optionals and variants are transparent. A name or an index applies to the contained value, or to the active
  alternative. An empty optional returns `NoActiveValue`.
- `Hidden` removes a field from listings only. A path can still reach it.
- `PathError::Offset` is the position in the path where the walk stopped, so that a tool can point at the bad step.

## Writing through the table

`SetValue(type, object, path, value)` writes one scalar. It makes every check before it writes, so a refused write
changes nothing. Tests compare the encoded bytes before and after each refusal.

| Field kind | Accepts | Refuses |
|---|---|---|
| Bool | a bool | everything else |
| Int | a signed or unsigned integer that fits the width and sign | a double, even a whole one |
| Float | any finite number, integers included | NaN, infinity, and a double too large for a float |
| Enum | a case name, matched exactly | an integer value |
| String | text, for `std::string` only | a write to a `string_view`, which is read-only |
| Struct, sequence | nothing: write to their fields and elements | every write |

- **`Range` is the limit that `SetValue` enforces.** Use `k_NoLimit` for an open end. A hard and a soft limit are a
  possible later addition.
- A `Range` on a sequence field applies to each element. `ReadOnly` applies to everything under its field.
- A float32 field compares at float precision. A limit of 0.1 is below `0.1f` as a double, and a comparison at double
  precision would refuse a value read back from the field.
- A span of const elements is read-only.

`SetValue` cannot resize a sequence, fill an empty optional, or change a variant's active alternative. No caller needs
them yet.

There are no locks. Tables are immutable, and the caller must own the object for the duration of the call.
