# Security Policy

OmniRender is a graphics post-processing engine. It does **not**
read or write game memory beyond the surface handles documented in
[`docs/ipc.md`](./docs/ipc.md), and it never executes arbitrary code
from the target process. That said, the project does load third-party
DLLs (ReShade, NVIDIA Streamline) and accepts HLSL/DXIL — both of
which have their own security profiles.

## Supported versions

| Version | Supported |
|---------|-----------|
| 1.0.x   |  Active |
| 0.x     |  Pre-release, no security backports |

## What is in scope

- **Memory safety bugs in `omnirender-hook.dll`** that could be
  triggered by a malicious game or by crafted D3D surface metadata.
- **Privilege escalation paths** where the daemon could be coerced
  into running code outside its intended sandbox.
- **DXGI handle leaks** that could be exploited by another
  co-resident process.
- **Build-time supply chain** — tampered DXC, MSVC, or CMake.

## What is out of scope

- Bugs in **NVIDIA Streamline / NGX / TensorRT**. Report those to
  NVIDIA.
- Bugs in **ReShade** or **MinHook**. Report those upstream.
- The behavior of a target game that happens to use OmniRender. We
  don't control the game.

## Reporting a vulnerability

**Do not** open a public issue for security reports. Use one of:

1. **GitHub private security advisory** (preferred):
   `https://github.com/GuruMachanica/OmniRender/security/advisories/new`
2. **Email:** see the maintainer contact on the GitHub profile.

You should receive an acknowledgement within 72 hours. We aim to
produce a fix or a mitigation plan within 14 days for critical
issues and 30 days for everything else.

## Hardening notes for users

- Run `OmniRenderDaemon.exe` as a normal user; it does not require
  administrator privileges.
- The hook DLL loads only OS-provided system libraries plus the
  modules you have explicitly enabled via CMake options.
- Do not place `omnirender-hook.dll` in any directory that other
  untrusted processes can write to. The Windows DLL search order
  will happily load it from the current working directory.