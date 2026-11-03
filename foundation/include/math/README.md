# DiamondDogs Math Library: Architecture & Rules

This library enforces a strict separation between **Storage Types** and **SIMD Execution Types**. To avoid performance footguns and compiler errors, strictly adhere to the following rules when interacting with this API.

## 1. Storage vs. SIMD Types (The Golden Rule)
*   **Storage Types:** `Float2`, `Float3`, `Float4`, `Float3x3`, `Float4x3`, `Float4x4`.
*   **SIMD Types:** `Vector`, `Matrix`, `Quaternion`, `VectorMask`.

**INVARIANT:** **NEVER** store `Vector`, `Matrix`, or `Quaternion` inside classes, structs, or standard containers. They are 16-byte aligned hardware registers meant exclusively for transient, hot-loop math. Always persist data using Storage Types.

## 2. The `auto` Proxy Trap (DANGER)
Methods like `Float4x4::Row()`, `Float4x4::Column()`, and the free function `FromVector()` return lightweight **Proxy Objects**, not concrete types. 

**INVARIANT:** **NEVER** use `auto` with these functions. You will capture a dangling proxy instead of the mathematical value, leading to compiler errors or undefined behavior.

```cpp
// WRONG: Captures a RowProxy or FromVectorProxy
auto myRow = mat.Row(0); 
auto myPos = FromVector(simdVec);

// RIGHT: Rely on implicit conversion to the explicit target type
Float3 myRow = mat.Row(0);
Vector myRowVec = mat.Row(0);
Float3 myPos = FromVector(simdVec);
```

## 3. When to use SIMD (The Rule of Three)
Do not eagerly convert to `Vector`/`Matrix` for everything. Moving data between standard memory (GPRs) and SIMD registers has a cost.

*   **Stay in Storage Types (`Float3`) if:** You are doing 1-2 simple operations (e.g., a single addition `pos += vel * dt`, or a single `Dot()` for a branch).
*   **Move to SIMD (`Vector`) if:** You are doing **3 or more** chained operations (e.g., Normalize, Cross, and Dot).
*   **ALWAYS use SIMD for:** 
    *   Matrix math. Matrix multiplication throughput is vastly superior in SIMD.
    *   Transcendental math. `Vector::SinCos()`, `Exp2()`, and `Log2()` process 4 lanes simultaneously using optimized polynomials.

## 4. Platform-Specific Heuristics: WASM vs. x86_64
*   **x86_64 (Desktop):** The CPU hides transition latencies well. The Rule of Three is forgiving.
*   **WASM (Web):** Memory-to-SIMD transitions are expensive. **Batch data, not operations.** Load a `Matrix` into SIMD *once*, then loop over contiguous arrays of `Float3` to transform them in-place. Minimize crossing the boundary.

## 5. Homogeneous/Affine Math Assumptions
The free `operator*` overloads for Storage Types make specific affine assumptions to save cycles:
*   `Float4x3 * Float4`: Assumes an implicit `(0,0,0,1)` 4th column. Passes `w` through unchanged.
*   `Float4x4 * Float3`: Drops the `w` component from the output. **No perspective divide is performed.**

If you need true perspective-correct homogeneous math (like `TransformCoord`), move to SIMD and use `Transform<3>()`.

## 6. Conversion Cheat Sheet

**To SIMD (Start of Math):**
```cpp
Vector v = ToVector(f3);
Matrix m = ToMatrix(f4x4);
```

**To Storage (End of Math):**
```cpp
Float3 f3 = FromVector(v);
Float4x4 f4x4 = FromMatrix<Float4x4>(m); // Explicit template required for Matrix return
```

**Constants:**
*   Storage: `Float3x3::Identity()`, `Float4x4::Zero()`
*   SIMD: `Vector::Identity()` `(1,1,1,1)`, `Vector::Zero()`, `Matrix::Identity()`
*   SIMD directions: `Vector::Up()`, `Vector::Forward()`, etc
    * World Space is strictly Right-Handed (+X Right, +Y Up, -Z Forward). Do not invert Z when adapting academic equations.
    * Exercise caution when referencing D3D or LH-coordinate-system maths, as conversion will be needed there.

Note that all matrix types are default-initialized (when not given constructor arguments) as identity matrices.
