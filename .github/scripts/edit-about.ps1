# filepath: .github/scripts/edit-about.ps1
# One-off helper: configures the GitHub repo "About" section.
# Run with: pwsh -File .github/scripts/edit-about.ps1
$ErrorActionPreference = "Stop"

gh repo edit GuruMachanica/OmniRender `
    --description "Out-of-process neural post-processing and temporal reconstruction engine for legacy 3D games (DirectX 8/9/10/11 + OpenGL). MIT-licensed." `
    --homepage    "https://github.com/GuruMachanica/OmniRender" `
    --add-topic graphics `
    --add-topic upscaling `
    --add-topic dlss `
    --add-topic fsr `
    --add-topic directx `
    --add-topic opengl `
    --add-topic windows `
    --add-topic cpp `
    --add-topic hlsl `
    --add-topic cmake `
    --add-topic neural-networks `
    --add-topic game-modding `
    --add-topic temporal-reconstruction `
    --enable-issues `
    --delete-branch-on-merge `
    --enable-squash-merge `
    --enable-auto-merge

Write-Host ""
Write-Host "Updated topics:" -ForegroundColor Cyan
gh repo view GuruMachanica/OmniRender --json repositoryTopics --jq ".repositoryTopics[].name"
