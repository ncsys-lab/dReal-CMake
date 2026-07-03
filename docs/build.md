# Build internals

The end-user build guide (prerequisites, native macOS/Linux, Docker) lives in `README.md`; the
agent quickstart in `CLAUDE.md` §Build. This file documents the build-system configuration for
developers modifying the build.

## `--version` output

`--version` prints three lines — feature flags, commit identity, and build platform:

```
dReal 5.0.0.1.<feature-flags>
Commit <hash> [<dirty>], Release Build.
Built for Darwin 25.5.0 arm64, on Jun 25 2026 12:51:30.
```

### How the values are wired in

- `<hash>` / `<dirty>` — captured at **every build** by `cmake/GenerateGitVersion.cmake`,
  which runs `git rev-parse --short HEAD` and `git status --porcelain --untracked-files=no`.
  This is a CMake `add_custom_target` (a build-time hook, **not** a git hook — it fires on
  `cmake --build`, not on `git commit`/`push`). The script writes `gcc_build/git_version.h`
  only when content changes, so `dreal_main.cc` is not recompiled unnecessarily.
- OS / arch — `CMAKE_SYSTEM_NAME`, `CMAKE_SYSTEM_VERSION`, `CMAKE_SYSTEM_PROCESSOR` injected
  as `target_compile_definitions` at CMake configure time.
- Timestamp — `__DATE__`/`__TIME__` compiler built-ins, stamped when `dreal_main.cc` is
  compiled (which happens whenever the hash/dirty status changes).
- Docker: `.git/HEAD`, `.git/refs/`, and a stub `objects/` dir are `COPY`'d into the image
  so `git rev-parse` works and the real hash appears. Dirty is always 0 in Docker (no index
  or object store to compare against).
