# Contributing to OmniRender

Thank you for your interest in contributing! OmniRender is a
research-grade graphics post-processing engine; thoughtful,
well-scoped pull requests are the most valuable form of contribution.

## Code of Conduct

All participants are expected to follow the
[`CODE_OF_CONDUCT.md`](./CODE_OF_CONDUCT.md). Be respectful, be
specific, assume good faith.

## How to report bugs

Use the **Bug report** issue template. Include:

- GPU model + driver version
- Game + version + render API (D3D9 / D3D11 / OpenGL)
- OmniRender build configuration (`-DOMNIRENDER_USE_NGX=ON` etc.)
- `OmniRenderDaemon.log` with `OMNI_LOG_DEBUG=1` if available
- Repro steps, expected vs. actual behaviour

## How to request features

Use the **Feature request** template. Describe the use case, the
expected outcome, and any prior art in other engines (DLSS, FSR 2,
XeSS) that might inform the design.

## Development workflow

```bat
:: 1. Fork and clone
git clone https://github.com/<you>/OmniRender.git
cd OmniRender
git remote add upstream https://github.com/GuruMachanica/OmniRender.git

:: 2. Branch
git checkout -b feat/short-descriptive-name

:: 3. Configure with tests
cmake -B build -A x64 -DOMNIRENDER_BUILD_TESTS=ON

:: 4. Iterate
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure

:: 5. Push and open a PR
git push origin feat/short-descriptive-name
```

## Coding standards

### C++

- **C++17 minimum.** No C++20-only features until VS 2022 17.6 is
  the minimum supported toolchain.
- **`/W4`** is enabled project-wide. Treat warnings as errors where
  feasible.
- **`NOMINMAX`** is defined project-wide; never `#undef` it.
- Prefer `omnirender::` namespace usage over `using namespace` in
  public headers.
- Use the `OMNI_LOG_*` macros from
  [`modules/common/logging.h`](./modules/common/logging.h). Do not
  introduce new logging dependencies.

### HLSL

- **Shader model `cs_6_5`** unless explicitly justified.
- 16×16 thread groups unless profiled otherwise.
- Document all resource bindings (`t*`, `u*`, `b*`) in the file header.

### Files

- Header guards via `#pragma once`.
- Every `.cpp`/`.h` file starts with a `// filepath: ...` comment.
- Every public surface change in [`modules/common`](./modules/common/)
  must be reviewed by **two** maintainers.

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(hook): add reverse-Z detection to depth locator
fix(daemon): pad DXGI shared handle before OpenSharedResource
docs(build): document NGX opt-in
chore: bump CMake minimum to 3.20
```

## Pull request checklist

- [ ] Tests added or updated
- [ ] `ctest` passes locally
- [ ] No new warnings under `/W4`
- [ ] `README.md` / `docs/` updated if user-facing
- [ ] Linked issue referenced (e.g. `Closes #42`)
- [ ] No proprietary NVIDIA binaries committed

## License

By submitting a pull request, you agree that your contributions are
licensed under the project's [GNU General Public License v3.0 (GPLv3)](./LICENSE).