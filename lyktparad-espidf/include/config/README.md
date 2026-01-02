# Configuration Files

This directory contains user-editable configuration files for the mesh network project.

## Files

- `mesh_config.h.example` - Template for site-specific mesh configuration (router SSID, mesh ID, etc.)
- `mesh_device_config.h.example` - Template for hardware-specific configuration (GPIO pins, LED settings)

## Setup

1. Copy the `.example` files to create your configuration:
   - `cp config/mesh_config.h.example config/mesh_config.h`
   - `cp config/mesh_device_config.h.example config/mesh_device_config.h`

   Or from the project root:
   - `cp include/config/mesh_config.h.example include/config/mesh_config.h`
   - `cp include/config/mesh_device_config.h.example include/config/mesh_device_config.h`

2. Edit the configuration files with your site-specific and hardware-specific settings.

3. **Note**: The `.h` files (without `.example`) are gitignored and should not be committed to version control.

For detailed configuration instructions, see the main README.md.
