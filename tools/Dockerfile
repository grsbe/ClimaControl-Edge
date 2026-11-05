# Build/test environment so nothing has to be installed on the PC except Docker.
# See tools/pio.ps1.
FROM python:3.12-bookworm
RUN pip install --no-cache-dir "platformio>=6.1,<7"
WORKDIR /work
ENTRYPOINT ["pio"]
