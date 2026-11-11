# Vendored FreeType

FreeType **2.14.1**, copied from https://gitlab.freedesktop.org/freetype/freetype
(`VER-2-14-1`). It is compiled directly into `Zekit.lib`, so downstream users do
not install FreeType separately.

## What was removed

The vendored copy is trimmed to what the build needs. These parts are gone and
must be restored from upstream if you ever need them:

| Removed | Why |
|---|---|
| `docs/oldlogs/` | Release notes from the FreeType 1.x era, no build or reference value |
| `src/tools/` | Development and maintenance tools (`glnames.py`, `apinames.c`, test programs). Nothing in the build references them |
| `src/bzip2/` | bzip2 support is not compiled; `FT_CONFIG_OPTION_USE_BZIP2` is off |

Everything else is untouched, including:

- `include/` — both the public headers (used by the wrappers, as `<ft2build.h>`)
  and `freetype/internal/`, which is required to compile the modules
- `src/` — every module in the build list, plus `dlg/` (only reached when
  `FT_DEBUG_LOGGING` is defined, kept so debug tracing can be turned on)
- `builds/windows/ftsystem.c` — the Win32 file-API implementation used on Windows
- `docs/` — `FTL.TXT`, `GPLv2.TXT`, `INSTALL.ANY`, `CUSTOMIZE` and the changelog

## Which modules are compiled

The list is in `cmake/ZekitSources.cmake`, function `zekit_freetype_sources`.
It follows upstream's `docs/INSTALL.ANY`, which is explicit that you must not
compile every `.c` file in the tree.

## License

FreeType is **not** MIT. It ships under the FreeType License (FTL) with a
GPLv2 alternative; Zekit uses the FTL option. The full text is in
`LICENSE.TXT` here and in `docs/FTL.TXT`. Redistributing a binary built from
Zekit must include the FreeType copyright notice — see
`THIRD_PARTY_NOTICES.md` at the repository root.

## Upgrading

1. Copy the new release over this directory.
2. Re-apply the three removals above.
3. Check `_mods` in `cmake/ZekitSources.cmake` against the new
   `docs/INSTALL.ANY` — module names do change between releases.
4. Build. A missing module shows up as undefined references at link time, never
   at compile time.
