---
name: Bug report
about: Report a defect in OmniRender
title: "[BUG] "
labels: bug
assignees: ''
---

## Summary

<!-- One or two sentences describing the defect. -->

## Environment

| Item | Value |
|------|-------|
| OmniRender version | (e.g. 1.0.0) |
| Commit SHA       | (run `git rev-parse HEAD`) |
| Build config     | (e.g. `-DOMNIRENDER_USE_NGX=ON -DOMNIRENDER_USE_TRT=ON`) |
| OS               | (e.g. Windows 11 23H2) |
| GPU              | (e.g. RTX 4050 Laptop) |
| GPU driver       | (e.g. 555.99) |
| Game             | (e.g. Skyrim SE) |
| Render API       | (D3D9 / D3D11 / OpenGL / Vulkan) |

## Steps to reproduce

1.
2.
3.

## Expected behaviour

<!-- What you expected to happen. -->

## Actual behaviour

<!-- What actually happened. Include screenshots / logs if relevant. -->

## Logs

```
<!-- Paste relevant `OmniRenderDaemon.log` content here (set OMNI_LOG_DEBUG=1 for verbose). -->
```

## Acceptance criteria affected

<!-- Optional: which QA-XX from docs/qa.md does this impact? -->

- [ ] QA-01 Address Space Safety
- [ ] QA-02 Hardware Profile Gating
- [ ] QA-03 Temporal Reconstruction
- [ ] QA-04 Broad API Compatibility
- [ ] None of the above