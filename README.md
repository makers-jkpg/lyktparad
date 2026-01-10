# Lyktparad 2026
This repository contains the lyktparad project, which has been split into two separate subprojects.

## Work in progress
This project is still under heavy development.

## Project Structure

- **`lyktparad-espidf/`** - ESP32/ESP32-C3 firmware implementation
  - ESP-MESH network with LED control
  - Embedded web server on root node
  - See `lyktparad-espidf/README.md` for details

- **`lyktparad-server/`** - External web server (future)
  - Optional external web server for enhanced UI
  - OTA firmware delivery (future)
  - Automatic discovery via mDNS

## Getting Started

For ESP32 firmware development, see `lyktparad-espidf/README.md`.

For external web server development, see `lyktparad-server/` (when available).
