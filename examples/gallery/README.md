# Control Gallery

One window, every widget in Zekit. Each card shows the control, the header it comes
from (`ZeButton.h`), and a live example: press it, drag it, type into it.

It doubles as a smoke test — if the gallery renders and responds, the library, the
renderer, the font stack and the input paths all work.

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

The library is pulled in as a subdirectory (`../..`), so nothing has to be built
first. On Windows the executable lands in `build/Debug/ZekitGallery.exe` or
`build/Release/`; on Linux in `build/ZekitGallery`.

To rebuild it together with the library:

```bash
cmake --preset windows-msvc -DZEKIT_BUILD_EXAMPLES=ON
cmake --build --preset windows-msvc-release
```

## What to look at

| Area | Shows |
|---|---|
| Sidebar | categories, jump to a group |
| Toolbar | theme switch (Dark/Light), language switch (EN/中文), renderer pill |
| Cards | one widget each, with the header name in the corner |
| Status bar | active backend, GPU name, DPI scale |

The language switch is live: every string goes through `Tr("english", "中文")`, so
the same build serves both languages. That is also why the sources carry Chinese
string literals — they are UI text, not comments.

## Shipping it to someone else

The default build uses the dynamic CRT, so the exe imports `VCRUNTIME140.dll` and
`MSVCP140.dll`. It runs on your machine because Visual Studio installed them; on a
clean machine it exits instantly with no window and no message.

For a binary you can hand out, build the static-CRT variant:

```bat
cmake --preset windows-msvc-mt -DZEKIT_BUILD_EXAMPLES=ON
cmake --build --preset windows-msvc-mt-release
```

Then confirm with `dumpbin /dependents` that no `VCRUNTIME`/`MSVCP` entry appears.
That exe is one file and starts on a bare Windows install.
