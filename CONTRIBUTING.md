# Contributing

Thanks for helping improve the native port or its preservation record.

## Before opening a change

- Use an issue for behavior differences and include the game, exact sequence, expected original
  behavior, actual port behavior, and a screenshot or short capture when useful.
- Keep reverse-engineering claims tied to a resource ID, Macintosh CODE offset, DOS overlay offset,
  trace, or repeatable original behavior.
- Do not add unrelated copyrighted dumps, disk images, executables, or replacement game assets.
- Do not run visual tests on somebody else's active desktop without their permission. The built-in
  regression suite is hidden and silent.

## Build and verify

From PowerShell with CMake, Ninja, Python, and MinGW-w64 available:

```powershell
.\tools\build_release.ps1
```

That command builds a static release, runs the dual-edition executable self-test, validates the PE
dependency boundary and both embedded packs, performs each full byte-preservation audit when its
ignored extraction manifest is present, repeats the test from an empty working directory, and
writes the release checksum.

At minimum, changes should build without warnings and pass:

```powershell
Start-Process .\build\MarioFundamentals.exe -ArgumentList '--self-test' -WindowStyle Hidden -Wait
python .\tools\verify_release.py .\build\MarioFundamentals.exe `
    --asset-pack .\assets\MarioFundamentals.pack `
    --asset-pack .\assets\MarioGameGallery.pack
```

## Pull requests

Keep each pull request focused. Explain the recovered source behavior, the implementation change,
and the test or evidence that prevents regression. By submitting Port Code, you agree that your
contribution may be distributed under the Port Code license in [LICENSE.md](LICENSE.md).
