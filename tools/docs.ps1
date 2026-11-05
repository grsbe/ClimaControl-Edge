# Builds or previews the documentation website (MkDocs + Material theme) in Docker.
#   .\tools\docs.ps1 serve    live preview at http://localhost:8000, reloads when a file changes (Ctrl+C stops)
#   .\tools\docs.ps1 build    static website in site\ ; open site\index.html, works offline
param([ValidateSet("serve", "build")][string]$cmd = "build")
$root = Split-Path -Parent $PSScriptRoot
$image = "squidfunk/mkdocs-material:9.5.50"
if ($cmd -eq "serve") {
    Write-Host "Documentation at http://localhost:8000 (Ctrl+C to stop)"
    docker run --rm -it -p 8000:8000 -v "${root}:/docs" $image serve --dev-addr 0.0.0.0:8000
} else {
    docker run --rm -v "${root}:/docs" $image build --strict
    if ($LASTEXITCODE -eq 0) { Write-Host "Done: open $root\site\index.html" }
}
exit $LASTEXITCODE
