# DiamondDogs - Agent Instructions

## Repository Context

DiamondDogs is a C++23 Vulkan engine that the author uses for research. It is not a production engine. Its current job
is to host the noise graph and planet generator. Shaders are Slang, cooked to SPIR-V by Lodestone (a sibling checkout
at `../Lodestone`) as a build step, and read at runtime through `rhi::ShaderLibrary`. Windows only, Vulkan 1.3+,
Visual Studio 18, Ninja Multi-Config. `CLAUDE.md` has the build, the tests, and the module map.

The sections "Response style" and "Code style" below are shared with Lodestone, copied from its
`.github/copilot-instructions.md` on 2026-10-20. Lodestone's copy is the source of truth: when it changes, copy it
again. File names in those sections (`Future.hpp`, `CookerErrors.hpp`, `ShaderToolsRanges.hpp`, `tools/shader_cooker`)
refer to Lodestone. The web-target notes apply to Lodestone's client; DiamondDogs is native only, but the frugal
memory rule stands.

## Response style

Follow Zinsser's four principles of quality writing:
1. Simplicity
2. Brevity
3. Clarity
4. Humanity

Additionally, communicate in ASD-STE100, or Simplified Technical English. Cut clutter, give each word one
meaning, and don't always reach for highly abstract verbiage. As a reminder some of those core rules are:

- Make instructions as clear and specific as possible.
- Do not write multi-word nouns that have more than three words.
- Use the approved forms of verbs to make only:
  - The infinitive form
  - The imperative form
  - The simple present tense
  - The simple past tense
  - The simple future tense
  - The past participle (only as an adjective)
- Do not use auxiliary verbs to make complex verb constructions.
- Use the "-ing" form of a verb only as a technical noun or as a modifier in a technical noun.
- Use the active voice. In descriptive writing, one should use the passive voice only when the agent is unknown.
- Write short sentences: no more than 20 words in instructions (procedures) and 25 words in descriptive texts.
- Do not omit parts of the sentence (e.g. verb, subject, article) to make the text shorter.
- Use vertical lists for complex text.
- Write one instruction per sentence.
- Write only one topic per paragraph.
- Do not write more than six sentences in each paragraph.
- Start safety or performance instructions with a clear command or condition.
  
## Code style

- Formatting: `.clang-format` — LLVM base, 4-space indent, 110-column limit, Allman braces, left-aligned
  pointers, always-break template declarations, no bin-packing of args/params.
- Static analysis: `.clang-tidy` is present and expected to be respected.

#### Code Formatting Rules
- **Single-line if statements**: NEVER allowed. All if statements must include brackets placed on a newline
- **Function implementations**: Eagerly define in source. No lazy implementations in headers — not getters,
  not setters, not one-line functions. Templates and `constexpr` functions intended for compile-time
  evaluation sometimes force our hand (`Future.hpp`, `SlotMap.hpp`, generated permutation-key code); that's
  just how it goes, and is not license to inline anything else
- **Indentation**: 4 spaces always (no tabs) for cross-platform consistency
- **Brackets**: Always go on new lines
- **Control Flow**: Always use braces for if statements, even single-line ones
- **Naming**: PascalCase for public APIs, camelCase for private members, snake_case for parameters
- **Single-word parameters**: snake_case and camelCase converge for a single token, so a single-word
  parameter can silently collide with a member of the same name. Prefix those with an underscore
  (`_instance`, `_createInfo`). Most compilers still resolve the member initialization correctly, but
  it is a silent killer when they don't, and the prefix is free. Multi-word parameters stay snake_case
  and need no prefix (`create_info`, `module_path`)
- **Member prefixes**: `m_` must NEVER be used as prefix for member variables
- **Array and aggregate initializers**: The opening brace goes on its own line, as every other brace
  does. One element per line when the list does not fit on one line:
```cpp
const std::array<const char*, 3> searchPaths
{
    sourceDirectory.c_str(),
    sharedDirectory.c_str(),
    cacheDirectory.c_str(),
};
```
- **Constructor initializers**: Colon on same line as declaration, each initializer on new line with trailing comma:
```cpp
Struct::Struct(int _val0, int _val1, int _val2) :
  val0{ _val0 },
  val1{ _val1 },
  val2{ _val2 }
{}
```
- **Switch Statements**: if a case is going to do more than return a value or call a function, pull that logic out into a separate function with a descriptive name. If brackets would need to be inserted to initialize variables in the case: pull it out into a separate function. Treat switch statements in this usage like a table of functions to be called
- **Comment usage**: Avoid as much as absolutely possible. Comments are no subsititute for descriptive code: I would rather have function names that are 80 characters long than comments that will rapidly drift from the source. Absolutely no comments depicting categories of code: that should be inferred from how functions are grouped (in the same order as they are declared, and in declaration order in the definition file)
- **Local variables**: If doing repeated operations, prefer longer variable names. use `deltaX` instead of `dX`, assign variables to const during long chains of mathematical operations (almost like writing scalarized SSA code), and favor being readable over being clever or taking shortcuts. We will save shortcuts and esoteric performant code for profiling results
- **Eagerly factor out common logic**: If some bit of code is greater than 4-5 lines and being duplicated, factor it out into a common function.
- **`todo` comments**: Spare these for things that are actually worth having greppable as distinct work items. For things that will need to be fixed before shipping this to customers or clients who are not devs or friends: use `todo-ship`. use `todo-perf` for things that could grant sizeable performance benefits. Minimize the usage of `todo` as much as possible: we need to get to MVP, but we also don't need to fill our backlog on that road.

#### Comment Style

Codified on 2026-10-15 from a review of agent-written comments against the author's rewrites. These rules
apply to code comments. Where they differ from the ASD-STE100 rules under "Response style" (passive voice,
vocabulary, sentence fragments), these rules win inside code. Write fewer comments first: the "Comment
usage" rule above still applies, and most comments that break the rules below should not exist at all.

- **Use the domain's exact term.** Write "file path", "normalized", "deduplicate", "canonical", even where
  a plainer English word exists. Do not substitute a plain word that is less exact: a path is not a "disk
  file", and a normalized path is not "one spelling".
- **Use literal verbs.** Write returns, contains, stores, lists, points to, is written to. Do not use data
  verbs as metaphors: "names", "holds", "gives", "states", "carries", and "owns" (outside its C++ meaning).
  - "holds" is replaced everywhere, even for a container: "the table contains", "the field stores".
  - "names" is fine when something is literally given a name ("Slang names every entry point `main`").
    When it means "identifies" or "refers to", rewrite it: "`RecordIndex` stores the index of the record".
  - Inline `//` comments can use ordinary idiom ("Tint gives us this for free"). The verb rules apply
    fully to doc comments.
- **Speak about a category in the plural, or state the fact.** Write "Embedded builtins are excluded", not
  "An embedded builtin is not on disk". The singular generic ("A cook reads...", "An axis holds...") reads
  like a statute.
- **Tie the purpose to the code.** Say what the code is for, with "This is used to", "so that", or "we do
  X so that Y". Do not state a consequence as a freestanding fact and leave the reader to connect it.
- **Give a reason only when a reader would ask why.** Do not attach a "so" or "because" to every statement.
- **No rhythm devices.** No echoed phrases ("once, in one spelling"), no three-part lists for cadence, no
  short closing sentence for effect, no colon that reveals a point. A colon before a real list is fine.
- **Keep the normal word order.** Write "every source read by the root module" or "every source that the
  root module read", not "every source the root module read". Do not frame a thing by what it is not when
  a positive statement works.
- **Voice.** "we" and "I" are fine. Passive voice is fine when the actor is obvious or unimportant
  ("Embedded builtins are excluded"). Do not force an active subject.
- **Sardonic is allowed, for the right occasion.** When C++, a third-party library, or a legacy interface
  forces something unfortunate or horrid, say so in the author's voice. For example: "This is truly,
  eminently, plainly, horribly bad, but I assure you: it's required", or "Thanks to the brilliant decisions
  of those who came before us, there's no better way to handle this". Keep it for the cases that earn
  it, and still say what the constraint is, so the next reader knows what not to "fix". Do not use it on
  ordinary code.
- **History notes are allowed, judiciously.** A note on what the code used to do ("the previous version
  was quadratic", "fixed: we used `getType()` here") explains the shape of the code and helps stop a
  regression. Keep these short, and keep them true: when the code changes again, update the note.
- **Reference only `todo.md`.** Code comments may point at `todo.md`, which is tracked. Do not reference
  anything under `docs/` (plans, phases, decision IDs such as "O7" or "F2.7"): that folder is gitignored,
  and its files move or are deleted. State the fact itself in the comment instead.
- **These rules bind what agents write.** Do not rewrite the author's own comments to fit them unless
  she asks.
- **Two registers.** A doc comment (`/** */`, `@brief`) uses full, capitalized sentences, and starts a
  function's brief with its action ("Returns..."). An inline `//` comment inside a body can be casual: a
  lowercase start, a fragment, an informal aside.

Examples, each written first in the style these rules replace:

```cpp
// Before:
/** The disk file of every source the module read. The depfile names them, so a build re-cooks when one
 * changes. An embedded builtin is not on disk and is not in the list. */
// After:
/** Returns the file path of every input source the module read, excluding embedded builtins. We write
 * these to the depfile so that the build step re-cooks when any input source changes. */

// Before:
// Every source file the cook read, for the depfile. Each target compiles the same files again, so the
// list repeats until the sort and unique below.
// After:
// input source paths for the depfile. each target reads the same files, so this has duplicates until
// the sort below

// Before:
/** @brief The one variant the query names. A query never chooses between variants in silence. */
// After:
/** @brief Returns the variant that matches this query. Fails when no variant or more than one variant
  * matches, instead of picking one. */
```

### **Anonymous Namespace Usage**

Helper functions placed in the anonymous namespace *must* be declared at the top of the file *only*, unless they are templates that cannot be defined elsewhere. The definition then *must* be at the bottom of the file. This avoids making the majority of a file that a user reads a wall of implementation details: seeing the declarations first tells them what the code will use, but focuses on the actual implementation in the object or `.cpp` as it's presented from the interfaces.

Examples of well formatted code (in Lodestone; DiamondDogs has no file yet that the author has marked as the example): `Future.hpp`, `InputManager.hpp` + `InputManager.cpp`, `Context.hpp` + `Context.cpp`. 

#### Integer Types
- **Signed by default.** Use `int32_t` for an index, a count, a loop counter, an offset, or anything
  that takes part in arithmetic. `int64_t` when the range needs it. This reverses the older habit in
  this codebase, and the older habit was wrong
- **Why.** Unsigned subtraction wraps silently, so `i - 1` at `i == 0` gives a huge number instead of a
  negative one. A mixed signed and unsigned comparison converts in a way few people predict: `-1 < 1u`
  is false. `ptrdiff_t` is signed, so every iterator distance forces a cast. A sentinel wants to be
  `-1`, not `UINT32_MAX`. Signed overflow is undefined, which reads like a drawback and is not: the
  compiler may assume it never happens, so it can strength-reduce and unroll a loop that an unsigned
  counter blocks
- **Four things stay unsigned**, and each one is load bearing:
  - A hash. `ContentHashValue` is `uint64_t`, and defined wraparound is the arithmetic FNV-1a asks for
  - A bitmask. A shift into the sign bit of a signed type is undefined
  - A serialized record. The manifest uses `uint32` cross-references, and `static_assert` pins each
    record size, because those bytes are a file format and not arithmetic
  - A byte size, where the value is a measurement of storage and never an index
- **The line to remember:** unsigned describes bytes and bit patterns. Signed describes arithmetic and
  indices
- Use `std::ssize` rather than `.size()` where a signed length is wanted. Cast a `size_t` from the
  standard library at the boundary, once, rather than letting it spread inward
- **Plain indices in a public API.** Name the field or the parameter for the table it indexes. Do not
  wrap an index in a type only to stop a wrong index, because each caller must then convert it. Use a
  wrapper type only when a mix-up fails in silence and the wrapper costs the caller nothing

#### C++ Language Preferences
- **Functions**: No implementations in headers; mark `constexpr` and `noexcept` when possible
- **Constructors**: Should be `noexcept` when possible
- **Move/copy operators**: Define `noexcept` versions when beneficial
- **Auto usage**: Minimize except for iterators/complex nested types (e.g., `auto iter = map.find(key)` OK, `auto value = vector.front()` not OK)
- **Virtual classes**: Use `final` when possible to collapse vtables and improve performance
- **Moving the value out of `std::expected`**: write `std::move(*result)`, not `std::move(result.value())`.
  Both select the rvalue overload. The dereference is the house style, and `value()` also carries a
  throwing path that a checked result never needs
- **Error handling**: Use `Result` types for function return status within RHI code; avoid exceptions. A result type is just `std::expected` with an error code enum as the unexpected value. When working outside the RHI, declare an error code for that subsystem and use that as appropriate. Don't leak error codes
- **Subsystem error pattern**: a subsystem outside the core RHI declares its own enum plus its own alias,
  and never borrows `RhiError`. `tools/shader_cooker` is the reference: `CookError` +
  `template<typename T> using CookResult = std::expected<T, CookError>;` in `CookerErrors.hpp`, with a
  `ToString(CookError)` declared there and defined in the matching source file
- **A function that yields nothing returns the error enum itself**, not `Result<void>`. `CookResult<void>`
  says only "this succeeds or it fails", and `CookError` already carries `Success` for that. Propagate
  between two such functions with a plain `return error;`, and wrap only at the boundary into a
  `Result<T>`, with `std::unexpected(error)`
- **Every such function must be `[[nodiscard]]`. No exceptions.** A bare enum return reads like a C
  status code, and a C status code gets ignored. The attribute is the only thing that stops it
- **Better than either: return nothing.** Before you pick a return type, ask whether the function can
  actually fail. A function whose inputs were validated at the boundary cannot, and it should return
  `void` or the value. Most of the friction that looks like "too many `Result` types" is really
  functions declaring an error they can never produce
- **Error logging**: For debug code or code that will be executed only on native, log with `std::println` frequently and often if it will help debugging. Same philosophy as comments though: do not fill it up for the sake of saying something.
- **Dynamic allocation**: avoid as much as possible, whenever possible. If required, allocate carefully, reserve upfront, and do not let memory persist
- Used ranged-for loops with the ranges library when possible, and as many of the algorithms header from that library as you can
- When writing output files, coalesce your writes into a single write operation by building the output piece
by piece, and then performing one final output step. This also reduces the number of times you  need to check
for valid paths and directories.

#### Enum Formatting
- Use smallest bitwidth type possible, always prefer `enum class` for scoping
- **Every enum reserves `0` for `Invalid`**, so a zero-initialized or memset value is never mistaken for
  a meaningful one. This mirrors how booleans behave: zero is the absence of a valid answer
- For result/error enums: `Invalid = 0`, then `Success` (or the equivalent "it worked" value) at `1`,
  then every error value beyond that
- For taxonomy enums (no success concept — `BindingKind`, `ShaderStageKind`, input event types): just
  `Invalid = 0`, then the values
- For bitmask enums: add operators for at least `|` and `&` operations
- Boolean conversion operators are preferable

#### Memory & Performance
- **Threading**: This project is not designed for massive threading but it is considered an important design goal
  that it is *thread-hardened*. Atomics are used where threads could compete over resources, and mutexes should
  be a reluctant object of absolute last resort. This app should be designed to scale to multiple threads, but 
  individual objects and functions should be viewed as single-threaded internally. Don't communicate by sharing
  memory, share memory by communicating.
  - **Message Passing**: as such, interfaces between modules of code that may need to talk to each  other should
    use message-passing paradigms. This allows for better thread isolation of work, and encourages a validate-
    try-commit model that is more recoverable.
  - **Atomics over mutexes**: always prefer atomics, ownership handoff (one thread owns the data until it hands it
    over with a release store), or a single-threaded coalesce step (for example at the start or end of a frame) to a
    mutex. A mutex serializes every thread that touches it. It also tends to spread, because the next fix adds a
    second lock. `rhi::GpuTimers` is the pattern: atomic compare-exchange to take a block, a release store to hand it
    back, and a coalesce step for everything shared.
  - **Tell the author about every mutex**: if you think a mutex (or `shared_mutex`, `condition_variable`, or any
    other lock) is needed, do not add it quietly. Say so in your reply, explain why atomics or ownership handoff do
    not work there, and get her agreement first. When you change code that already has a lock, say so too.
- **Memory**: Avoid dynamic allocations as often as possible. On web targets, we are running in a virtual env
  with a pre-allocated linear span of memory. We must be frugal with memory, and should favor using statically
  allocated arenas and being efficient with our choice of datatypes.
- **Span**: Use `std::span` for array parameters instead of raw pointers + size, and for passing ranges
  of values between systems. 
- **String conversion**: Use `charconv` instead of C conversion functions for string/char to integral types
- **Error Handling**: Use `Result` types for function return status within RHI code; avoid exceptions. For code outside core Rhi, create a new enum class and use it with `std::expected` for error handling. Bubble up errors to the caller instead of logging and returning a default value.

#### Error Handling
- **Validate at the ingestion surface. Trust inside.** Treat every input that arrives from outside --
  a command line, a file, a socket, a compiler API -- as untrusted, and check it once, there. Sanitize
  it where sanitizing cannot break another promise. After that boundary the data is known good, and a
  function that re-tests it adds a code path nobody can reach and nobody tests
- **Keep every validator. Delete every defensive check.** These look alike and are not the same thing:
  - A **validator** compares two answers that were derived independently. `VerifyLibraryRoundTrip`,
    `ValidateVariantReflection`, `VerifyManifestRoundTrip`, and `--verify-deterministic` are the whole
    correctness argument of this repository. They are internal, they look redundant, and they stay
  - A **defensive check** re-tests an invariant the code already established. An index that this code
    put in a table does not need a bounds test before this code reads it back. If it is out of range,
    the answer is a defect, not a `false`
- **An error message is user feedback first.** Write the record for the person who has to fix the
  shader, and name the file, the entry point, and the value. A stage builds the record; a sink decides
  what it looks like. Compilation must never format a string
- **Result<T>**: Use a rust-like Result<T> bubbled up through functions to return a value or an error
  code to users when the function actually returns a *discrete result type* `T`. As mentioned above, return
  an error code itself and enforce `[[nodiscard]]` at all times when doing this to ensure it is not ignored.
- **Error Values**: Use an enum class of the minimum width required to convey the systems range of errors.
  Use 0 as an invalid initial value, 1 as success, and pin all further error codes to be > 1
- **Error Messages**: For `enum class` error values, provide an enum-to-stringview conversion function in
  a header that uses magic_enum in the source to retrieve the enum name. Further information may be appended
  to the view, but careful consideration of lifetime of error strings should be considered
- **Exceptions**: Exceptions are a tangled mess on web, especially with how event-loop-driven our app is.
  Avoid them as much as possible, and attempt to provide a way for modules of code to shutdown and restart
  in a known-good state to recover from errors.

#### Accessor Chaining

When writing a function that returns a value that can be accessed via operators like `[]`, instead of using it inline like:

```cpp
auto value = Getter()[index];
```

or 

```cpp
return SomeNewStruct{ .Field = Getter()[index] };
```

Prefer to do the following, so it's much clearer what is being accessed and how the value is obtained:

```cpp
std::span<MyStruct> values = Getter();
return SomeNewStruct{ .Field = values[index] };
```

Don't short-circuit or shortcut into accessors like the initial examples were, ever, as it introduces ambiguity and potential lifetime issues with the accessed values: as well as just being generally ugly. The extra line is not going to actually change the generated assembly, but it will make the intent and the lifetime of the accessed values much clearer.

#### Header Conventions
- Headers carry **both** `#pragma once` and a traditional `#ifndef`/`#define`/`#endif` guard. This is
  intentional and consistent across the repo — do not "clean it up" to one or the other. Guard macros are
  screaming-snake-case derived from the path (`LODESTONE_ASYNC_FUTURE_HPP`,
  `LODESTONE_ERRORS_HPP`), and the closing `#endif` carries a `// !GUARD_NAME` comment
- Keep standard-library includes minimal and sorted within a group; a header should include what it
  names and nothing more. Prefer forward declarations across module boundaries (see how `Context.hpp`
  forward-declares `Scheduler`)
- Hide third-party types behind a pimpl when a header would otherwise force them on every consumer.
  `tools/shader_cooker/include/SlangCompiler.hpp` names no Slang type for exactly this reason

### C++ Standard Library Usage
- Use `std::upper_bound` and `std::lower_bound` from `<algorithm>` when possible
- Retrieve numerical constants from `<numbers>` header
- Minimize standard library includes across module boundaries

#### Ranges and Views
 
Favor `std::ranges` algorithms over the unconstrained `<algorithm>` overloads. Favor a view pipeline over
a hand-written loop when the pipeline says what the code does more clearly. This supersedes the older line
"Used ranged-for loops with the ranges library when possible", which was too vague to act on.
 
Views are correct for marshaling, reflection, manifest assembly, and any transformation between pipeline
stages. Views are not correct in a per-frame or per-instruction loop. This library has no such loop today.
Write for clarity first, and apply the rules below to avoid the cases that cost real time.
 
- **Prefer a view when it removes a manual counter or a manual index.** `std::views::enumerate` removes an
  index that a `continue` can desynchronize. `std::views::transform` with a pointer to a data member
  extracts one field and compiles to one load
- **Prefer `std::ranges::to` over a manual fill loop**, but read the reserve rule below first
- **Write a loop instead when the pipeline needs more than three adaptors.** A deep pipeline depends on the
  inliner, and the inliner gives up. A loop states the same thing and never surprises a reader

##### Two families of adaptor
Know which family an adaptor belongs to. The family decides the cost.
 
- **Index adaptors** keep `sized_range` and `random_access_range`: `transform`, `take`, `drop`, `stride`,
  `zip`, `iota`, `enumerate`, `elements`, `as_rvalue`. These cost nothing after optimization. Use them
  freely
- **State machine adaptors** drop `sized_range` and collapse the range category: `filter`, `join`, `split`,
  `take_while`, `drop_while`, `chunk_by`. Each one holds a loop inside `operator++`. Use one per pipeline,
  and put it last

##### Rules
 
- **Call the expensive function once.** `transform_view::operator*` reruns the callable on every
  dereference, and it caches nothing. A `filter` after a `transform` reruns the transform for each element
  that passes. Reorder to `filter | transform` when the predicate reads the untransformed element
- **Use `tk::cache_latest` when you cannot reorder.** The shim in `ShaderToolsRanges.hpp` forwards to
  `std::views::cache_latest` where the compiler has it. MSVC does not have it yet, so the shim compiles to
  a no-op there and the transform runs twice. Add a `todo-perf` at each such site
- **Pin the return type of a lambda that returns a container.** A deduced return type strips the reference
  and copies the container. Write `-> const std::vector<uint32_t>&` explicitly. This mistake is silent, and
  it also forces `join_view` to hold a cache it would not otherwise need
- **Reserve before `insert_range`, `append_range`, or `ranges::to` when the range is not sized.** Anything
  past a `filter` or a `join` has no size, so the container grows one reallocation at a time. Total the size
  with `std::ranges::fold_left` and reserve once. This matches the existing rule on dynamic allocation
- **Build a `filter` pipeline once.** `filter_view::begin()` is O(n), and the cache that hides that cost
  resets on copy and on move. Pass the pipeline by reference. Never rebuild it inside a loop
- **Never call `ranges::distance` or `ranges::size` on a range past a `filter`.** The call is O(n). The same
  call in a loop condition is O(n squared)
- **Never adapt a `std::generator` or any coroutine range.** Each increment resumes a coroutine frame, and
  the optimizer crosses no suspension point
- **Never put `std::function` or a virtual call inside a `transform`.** The call is opaque, so the compiler
  cannot fold the repeated dereference. Dispatch once at the stage boundary instead
- **Parse text with `std::string_view::find`.** `lazy_split_view` walks one character at a time and is much
  slower than the loop it replaces

##### Interaction with other rules in this file
 
- **A view type has no name, and this repository forbids an implementation in a header.** Do not return a
  raw pipeline from a function that a header declares. Declare a named functor and a type alias instead, as
  `GatheredSpan` does, so the header names the type and the definition stays in the source file
- **`enumerate` yields a signed index.** The type is `std::ptrdiff_t`, which agrees with the signed-by-default
  rule. Cast once at the boundary where an unsigned serialized field needs it
- **Bind the element by reference.** Write `for (auto&& [index, element] : std::views::enumerate(range))`.
  A plain `auto` copies each element
- **A view holds a reference, and it owns nothing.** Never store a pipeline in a member, and never return one
  that outlives its source container. Materialize with `ranges::to` when the result must outlive the tables
- **Debug builds pay for every layer.** Each adaptor is a real call at `/Od`, and MSVC is the worst case. Keep
  pipelines shallow in any code path that a developer runs in a Debug cook

##### Selecting rows from a global table
 
This library stores data in global tables and stores local index lists against them. Gather through a view
rather than materializing a vector of pointers. The index list already holds the information, and a
`transform` over it stays sized, stays indexable, and allocates nothing. Return `const T&` from the
projection, not `const T*`. Materialize only when the caller sorts the result, mutates it, crosses a thread
boundary, or outlives the table.

## DiamondDogs conventions

These come from the earlier DiamondDogs instructions and still apply. Where they differed from the shared rules
above, the shared rules now win, and the difference is listed under "Superseded" so that it is not lost.

### Foundation and modules

- `foundation` is the engine's standard library: lock-free MWSR queues, critical sections, SRW locks, circular
  buffers, delegates, tagged types, hashes, math, image formats. Use foundation's delegate instead of
  `std::function`.
- Modules are static libraries under `modules/`. Foundation, then modules, then applications, then tests.
- Modules talk through messages (the MWSR queue). `RhiResources` is the reference: calls become messages, a worker
  owns the state, and replies carry the result.
- Typed handles (`rhi::RhiHandle<Tag>`) keep graphics API types out of public headers.
- Platform code goes through PlatformSystem, not direct GLFW. ImGuiModule is the one exception, because the
  official GLFW backend needs the window.

### CMake

- Use `add_custom_command` with `OUTPUT` and `DEPENDS` rather than `add_custom_target`, so that slow generators run
  only when their inputs change. Track explicit dependencies on external files (`vk.xml`).
- Use `VERBATIM`.
- `cmake -E copy` fails on an empty list. Guard a copy of `$<TARGET_RUNTIME_DLLS:...>` (see
  `tests/integration_tests/CMakeLists.txt`).
- `MSVC` is true for clang-cl too. Use `DIAMOND_DOGS_COMPILER_IS_CL` for what only cl.exe has (`/MP`, SVML), and
  build with both presets.

### Math

- Row-major matrices, right-handed coordinate system. Use the "RH" versions of matrix functions.
- Slang: `transformed = mul(vec, mat)`, with the vector as a row vector.
- Depth range: [-1,1] (author, 2026-10-20). On Vulkan this needs `VK_EXT_depth_clip_control`
  (`negativeOneToOne`). An open objection (reversed-Z with [0,1] gives more precision) is in `docs/audit.md` section 10.

### Slang shaders

- `.slang` files. Do not give a file the name of one of its entry points (it confuses slangc tooling).
- Lodestone owns permutation: axes are declared in the shader (`extern static const`), and policy lives in a TOML
  file. Read Lodestone's `CLAUDE.md` before adding an axis. Do not add a `[SpecializationConstant]` axis by hand.
- `import moduleName;` for modules, `__include fileName;` for fragments of one module. Mark helpers `internal`.
- Prefer wave operations (`WaveActiveSum`, `WavePrefixSum`, `WaveIsFirstLane`). Reduce within a wave first, then
  across waves. Minimize barriers; compute per thread when the arithmetic is cheaper than shared memory plus a
  barrier. `GroupMemoryBarrierWithGroupSync()` for full synchronization, `GroupMemoryBarrier()` for visibility only.
- Buffer device address and bindless are not designed yet (the author owns that design). Lodestone refuses Pointer
  and Handle placements on SPIR-V today, and `ResourceContext` refuses `DeviceAddressBuffer`.

### Validation

- A test passes only with exit code 0 and no validation message in its output (see `CLAUDE.md`).
- `ValidationLayers::WithSynchronization` and above turn on synchronization validation. No environment variable.
- Run the windowed tests with `--frames=N`, and with `--resize` to exercise swapchain recreation.

### Superseded

- Enums: the earlier rule was "`None = 0` for distinct values, `Invalid = 0` for bitmasks". The shared rule is
  "every enum reserves 0 for `Invalid`". Older DiamondDogs enums (`ImageAspectFlags::None`, `ComponentSwizzle::None`,
  `ResourceCreationFlags::None`) predate it.
- Build: the earlier file used the Visual Studio 2022 generator in `build/` and VS Code tasks. The build is now Ninja
  Multi-Config in `build/ninja-msvc` (see `CLAUDE.md`).
- Slang: the earlier file described a runtime Slang compiler and `.slang-module` rebuilds. Both are gone; Lodestone
  cooks every shader.
## PR Review Behavior
When reviewing a PR, apply the /caveman skill from skills/caveman to provide terse, technical feedback. Focus on the substance of the code and avoid any filler or pleasantries. Use the project's domain glossary vocabulary to maintain consistency. Be terse and accurate - respond like a senior engineer who has built a rapport with me and can cut to the chase without worrying about tone. 
