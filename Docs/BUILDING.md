# Building

Native, on macOS. No Docker, no container, no cross build: clang and lld from
Homebrew, EDK2 BaseTools compiled from the pinned source, and an object tree
that carries over between builds.

## Once

```sh
Tools/setup-build.sh
```

Pins every submodule including nested ones, and satisfies the host-specific
external dependencies Mu publishes for Linux and Windows only. It uses a sibling
checkout as a reference when it finds one, so it needs no network in that case.

## Then

```sh
Tools/mu-build j414s
```

That is the configuration known to boot on that machine. Each flag moves one
variable:

```sh
Tools/mu-build j414s --without wireless
Tools/mu-build j414s --with media
Tools/mu-build j414s --with media --without gpu-acpi
Tools/mu-build --list
```

There are no profiles. A build is a device plus the features that are on, and
`--list` prints both. Devices and features live in `Platform/Features.py`;
adding a Mac is an entry there.

Output goes to the main AuroraSilicon repository, keyed by configuration and
commit:

```text
../AuroraSilicon/build/mu/<configuration>/<commit>/artifacts/
```

`AURORADBG_MU_OUTPUT_ROOT` overrides the root.

## Speed

Builds are incremental. The object tree for a configuration is seeded from the
previous build of that same configuration, so a rebuild after a source change
recompiles what changed:

```text
Seeding incremental Mu build from .../<previous-commit>
Building j414s with native CLANGPDB
```

A no-op rebuild is about 35 seconds, nearly all of it EDK2 re-walking the DSC.
A first build for a new configuration is a few minutes. `stuart_setup` and
`stuart_update` run once per output tree and are then skipped; set
`NTASI_MU_NATIVE_REFRESH=always` to force them.

Requirements: Python 3.11 or 3.12, Homebrew LLVM and lld, `iasl`, `nasm`,
`make`. Only DEBUG/CLANGPDB is supported.

## What was built

Every build writes `manifest.json` beside the firmware, recording the device,
which features were on and off, the resolved defines, the source commit and tree,
and the SHA-256 of the image. Nothing in it is asserted against a pinned
expectation -- it says what happened.
