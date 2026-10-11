# tiny-optional (vendored)

Upstream: https://github.com/Sedeniono/tiny-optional
Version:  v1.5.3
License:  Boost Software License 1.0 (see LICENSE)

Two headers, no build step, so they sit in the tree and are included as
`<tiny/optional.h>` — `src/shared` is already a PUBLIC include dir on
`game_shared`, so nothing in CMakeLists.txt refers to this at all. The
FetchContent recipe that would replace it is written out, commented, beside the
other dependencies (block 7).

To update: re-download both headers plus LICENSE at the new tag and bump the
version above. Nothing here is patched.

## What it buys, measured with this toolchain (clang 19, MSVC ABI, C++23)

| payload                          | `std::optional` | `tiny::optional` |
|----------------------------------|-----------------|------------------|
| `bool`                           | 2               | 1                |
| `float`                          | 8               | 4                |
| `double`                         | 16              | 8                |
| `void*`                          | 16              | 8                |
| `uint32_t`                       | 8               | 8                |
| `uint32_t` + a named sentinel    | 8               | 4                |
| `linalg::vec3` + a manipulator   | 16              | 12               |
| `size_t`, `std::string`          | 16 / 40         | 16 / 40          |

The rows that shrink on their own are the ones with a spare bit pattern the
library already knows about (a NaN, an invalid address). Everything else needs
the sentinel spelled at the use site — `tiny::optional<uint32_t, UINT32_MAX>` —
or, for a type whose spare bits are inside a member, a
`tiny::optional_flag_manipulator<T>` specialization.

`linalg::vec3` needs the manipulator rather than the member-pointer spelling
(`tiny::optional<vec3, &vec3::x>`): `x` is a member of an anonymous union, and
a pointer-to-member cannot be formed for one.

## The cost

Including this header costs ~65ms per TU over `<optional>` alone. That is the
reason to include it in the TU that uses it and NOT from a widely-included
header — at ~200 TUs a blanket include is ~13s of build time to save bytes that
mostly are not stored anywhere.
