# Plugin API Data Endpoint Integration Test Notes

## Overview

Integration tests for the Plugin API Data Endpoint require hardware-in-the-loop testing with actual ESP32 devices in a mesh network configuration.

## Prerequisites

1. **Hardware Setup**:
   - ESP32 root node with firmware flashed
   - At least 2 child nodes in mesh network
   - Network connectivity to ESP32 root node
   - External web server running (optional, for proxy testing)

2. **Software Setup**:
   - At least one plugin registered (e.g., `rgb_effect`, `sequence`)
   - HTTP client tools (curl, Postman, or browser)
   - Network access to ESP32

## Test Scenarios

### 1. Complete Request Flow

**Objective**: Verify end-to-end data flow from HTTP request to mesh nodes.

**Steps**:
1. Send POST request to `/api/plugin/rgb_effect/data` with test data
2. Verify 200 OK response
3. Verify data received by all child nodes
4. Verify command format: `[PLUGIN_ID:1] [PLUGIN_CMD_DATA:1] [RAW_DATA:N]`

**Expected Results**:
- HTTP response: 200 OK with `{"success":true}`
- All child nodes receive command
- Command format matches specification
- Data integrity preserved

### 2. Zero-Length Data Flow

**Objective**: Verify zero-length data creates 2-byte command.

**Steps**:
1. Send POST request with empty body (0 bytes)
2. Verify 200 OK response
3. Verify 2-byte command sent to mesh: `[PLUGIN_ID:1] [PLUGIN_CMD_DATA:1]`

**Expected Results**:
- HTTP response: 200 OK
- 2-byte command sent to mesh
- Root node processes command locally

### 3. Data Integrity Verification

**Objective**: Verify no data corruption through entire chain.

**Steps**:
1. Send known data pattern (e.g., `[0xFF, 0x00, 0x80, 0x40]`)
2. Capture command at mesh level
3. Verify data bytes match exactly

**Expected Results**:
- Data forwarded without modification
- No encoding/decoding occurs
- Raw bytes preserved

### 4. Error Propagation

**Objective**: Verify errors are returned correctly.

**Test Cases**:
- Invalid plugin name (404)
- Invalid Content-Type (400)
- Data too large (413)
- Mesh busy (503)
- Plugin not found (404)

**Expected Results**:
- Correct HTTP status codes
- Descriptive error messages
- Error responses in JSON format

### 5. Root Node Local Processing

**Objective**: Verify root node processes command locally.

**Steps**:
1. Send data to endpoint
2. Verify root node's plugin command handler is called
3. Verify command also forwarded to child nodes

**Expected Results**:
- Root node processes command locally
- Command forwarded to child nodes
- Local processing doesn't block forwarding

## Limitations

- Integration tests require actual hardware
- Cannot be run in CI/CD without hardware setup
- Requires manual verification of mesh command reception
- Test execution time depends on mesh network stability

## Future Enhancements

1. Automated test scripts for common scenarios
2. Hardware abstraction layer for test automation
3. Command capture/logging infrastructure
4. Automated data integrity verification
