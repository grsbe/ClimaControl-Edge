# Runs PlatformIO inside Docker, so only Docker is needed on the PC.
#   .\tools\pio.ps1 test -e native     unit tests
#   .\tools\pio.ps1 run -e esp32dev    compile the firmware (flashing needs USB: use the VS Code extension)
$root = Split-Path -Parent $PSScriptRoot
$image = docker images -q climacontrol-pio
if (-not $image) { docker build --load -t climacontrol-pio "$PSScriptRoot" }
docker run --rm -v "${root}:/work" -v climacontrol-pio-cache:/root/.platformio climacontrol-pio @args
exit $LASTEXITCODE
