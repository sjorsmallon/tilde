// The `embed` mode's one file, and it is deliberately tiny: a package is a
// contiguous byte range, so all this mode does is name a different range. Every
// line that reads it is shared with `pkg` (see asset_source_t).
//
// Compiled ONLY when TILDE_ASSET_SOURCE is embed. In the other two modes
// embedded_asset_package() is a declaration nothing calls and nothing links --
// which is what keeps a 40MB .rodata array out of a dev build.
//
// The package is found through --embed-dir rather than a relative path, so the
// generated file's location is the build's business and not this file's.

#include "../asset_package.hpp"

#if !__has_embed(<assets.pkg>)
#error "assets.pkg is not on the embed path -- the asset_package build target did not run"
#endif

namespace assets
{

namespace
{

// alignas so the index inside the package starts aligned even though nothing
// below the byte layer depends on it: entries are read out with memcpy, and this
// is here so the DATA spans handed to decoders are aligned the way a file read
// would have made them.
alignas(16) const uint8_t ASSET_PACKAGE_BYTES[] = {
#embed <assets.pkg>
};

} // namespace

Span<const uint8_t> embedded_asset_package()
{
  return Span<const uint8_t>(ASSET_PACKAGE_BYTES, (uint32_t)sizeof(ASSET_PACKAGE_BYTES));
}

} // namespace assets
