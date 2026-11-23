# Full-system simulation of the gamepad:
#   stage 1 builds the real firmware image from source;
#   stage 2 runs it on an emulated STM32F411 and lets the Linux USB/HID
#   stack enumerate it over USB/IP (vhci_hcd is built into Docker Desktop's kernel).
FROM debian:stable-slim AS firmware
RUN apt-get update && apt-get install -y --no-install-recommends \
      cmake make gcc-arm-none-eabi libnewlib-arm-none-eabi python3 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt CMakePresets.json ./
COPY cmake cmake
COPY core core
COPY firmware firmware
COPY policy policy
COPY tools/size_budget.py tools/size_budget.py
RUN cmake --preset firmware >/dev/null && cmake --build --preset firmware \
    && python3 tools/size_budget.py --profile policy/hardware-profile.toml \
         build/firmware/firmware/stm32f411/gamepad.elf

FROM debian:stable-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      python3 python3-venv python3-evdev usbip kmod \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv --system-site-packages /opt/venv \
    && /opt/venv/bin/pip install --no-cache-dir unicorn==2.1.1 pyelftools==0.31
ENV PATH=/opt/venv/bin:$PATH PYTHONUNBUFFERED=1
WORKDIR /sim
COPY --from=firmware /src/build/firmware/firmware/stm32f411/gamepad.elf /sim/gamepad.elf
COPY sim/emu /sim/emu
ENTRYPOINT ["python3", "-m", "emu.e2e"]
