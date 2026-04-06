# ch592_usbhid

USB HID device firmware for the **WCH CH592** RISC-V microcontroller. The firmware enumerates as a custom HID device (vendor-defined usage page `0xFF00`) with bidirectional EP1 interrupt endpoints (64 bytes each) and periodically uploads 4-byte reports to the host.

## Features

- Full USB 2.0 Full-Speed HID device stack implemented in bare-metal C
- Custom HID report descriptor (vendor usage page `0xFF00`, 64-byte IN/OUT reports)
- EP0 control transfers: standard USB requests + HID class requests (`SET_IDLE`, `SET_REPORT`, `SET_PROTOCOL`, `GET_IDLE`, `GET_PROTOCOL`)
- EP1 IN — periodic HID report upload (interrupt transfer)
- EP1 OUT — echo-back with bitwise inversion; LED on PA4 reflects bit 0 of the first received byte
- Remote wakeup support
- UART1 debug output via `printf` (PA9 TX / PA8 RX)
- Version tracking via [`version`](version) file

## Hardware

| Item           | Value                            |
| -------------- | -------------------------------- |
| MCU            | WCH CH592 (RISC-V, `rv32imacxw`) |
| USB D+ pull-up | internal (`RB_PIN_USB_DP_PU`)    |
| Debug UART     | UART1 — TX: PA9, RX: PA8         |
| LED            | PA4 (active-low)                 |
| System clock   | 60 MHz (PLL)                     |

## USB Descriptors

| Descriptor      | Value                   |
| --------------- | ----------------------- |
| VID             | `0x4348`                |
| PID             | `0x5537`                |
| Manufacturer    | `wch.cn`                |
| Product         | `ch592_testHID`         |
| Class           | `0xFF` (vendor-defined) |
| EP0 size        | 64 bytes                |
| EP1 IN/OUT size | 64 bytes, interval 1 ms |

## Project Structure

```
ch592_usbhid/
├── app/
│   └── main.c          # Application: USB stack, HID reports, main loop
├── vendor/
│   ├── Ld/Link.ld      # Linker script
│   ├── RVMSIS/         # RISC-V CMSIS headers
│   ├── StdPeriphDriver/ # CH592 peripheral driver library + libISP592
│   └── Startup/
│       └── startup_CH592.S  # Reset/startup assembly
├── Makefile            # Top-level build configuration
├── rules.mk            # Generic GCC build rules
└── version             # Firmware version number
```

## Requirements

- **Toolchain**: `riscv32-wch-elf-gcc` (GCC 15) — or adjust `GCC_PREFIX` in [`Makefile`](Makefile) for GCC 12 (`riscv-none-elf-`) / GCC 11 (`riscv-none-embed-`)
- **Flasher**: [`wchisp`](https://github.com/ch32-rs/wchisp) — USB ISP mode

## Build

```bash
# Clean build artifacts
make clean
# Default build (produces .elf, .bin, .hex in _build/)
make -j16
# Via USB ISP (wchisp)
make flash
```

## Firmware Logic

[`main()`](app/main.c) initialises the system clock at 60 MHz, sets up UART1 for debug output, configures endpoint RAM buffers, calls `USB_DeviceInit()`, enables the USB IRQ, then enters an infinite loop that sends four alternating 4-byte HID reports whenever the device is ready:

```
0x05 0x10 0x20 0x11
0x0A 0x15 0x25 0x22
0x0E 0x1A 0x2A 0x44
0x10 0x1E 0x2E 0x88
```

[`DevEP1_OUT_Deal()`](app/main.c) handles data received from the host: it toggles the LED based on bit 0 of the first byte and echoes back the bitwise-inverted payload.

## Version

Current version: **1000** (see [`version`](version)).  
The version is embedded into the firmware at compile time via the `-DSW_VERSION_=` preprocessor define.
