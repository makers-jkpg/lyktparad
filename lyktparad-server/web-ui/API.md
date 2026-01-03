# API Documentation

This document describes the API endpoints used by the web UI. All endpoints use relative paths starting with `/api/` and will work through the external web server proxy when implemented.

## Endpoints

### GET /api/nodes

Returns the number of nodes in the mesh network.

**Response:**
```json
{
  "nodes": 5
}
```

**Status Codes:**
- `200 OK` - Success
- `500 Internal Server Error` - Server error

---

### GET /api/color

Returns the current RGB color values.

**Response:**
```json
{
  "r": 255,
  "g": 128,
  "b": 64,
  "is_set": true
}
```

**Status Codes:**
- `200 OK` - Success
- `500 Internal Server Error` - Server error

---

### POST /api/color

Sets the RGB color for all mesh nodes.

**Request Body:**
```json
{
  "r": 255,
  "g": 128,
  "b": 64
}
```

**Response:**
```json
{
  "success": true
}
```

**Status Codes:**
- `200 OK` - Success
- `400 Bad Request` - Invalid RGB values (must be 0-255)
- `500 Internal Server Error` - Server error

**Error Response:**
```json
{
  "success": false,
  "error": "RGB values must be 0-255"
}
```

---

### POST /api/sequence

Synchronizes sequence data (tempo, row count, and color grid data) to the mesh network.

**Request Body:**
Binary payload (Uint8Array):
- Byte 0: Tempo value (10ms units, range 1-255, representing 10-2550ms)
- Byte 1: Number of rows (1-16)
- Bytes 2+: Packed color data (3 bytes per 2 squares)

**Response:**
```json
{
  "success": true
}
```

**Status Codes:**
- `200 OK` - Success
- `400 Bad Request` - Invalid request format
- `500 Internal Server Error` - Server error

**Error Response:**
```json
{
  "success": false,
  "error": "Invalid request format"
}
```

---

### GET /api/sequence/pointer

Returns the current sequence pointer position.

**Response:**
Plain text number (0-255)

**Status Codes:**
- `200 OK` - Success
- `500 Internal Server Error` - Server error

---

### GET /api/sequence/status

Returns the current sequence status (active/inactive).

**Response:**
```json
{
  "active": true
}
```

**Status Codes:**
- `200 OK` - Success
- `500 Internal Server Error` - Server error

---

### POST /api/sequence/start

Starts the sequence playback.

**Response:**
```json
{
  "success": true
}
```

**Status Codes:**
- `200 OK` - Success
- `500 Internal Server Error` - Server error

**Error Response:**
```json
{
  "success": false,
  "error": "Error message"
}
```

---

### POST /api/sequence/stop

Stops the sequence playback.

**Response:**
```json
{
  "success": true
}
```

**Status Codes:**
- `200 OK` - Success
- `500 Internal Server Error` - Server error

**Error Response:**
```json
{
  "success": false,
  "error": "Error message"
}
```

---

### POST /api/sequence/reset

Resets the sequence pointer to the beginning.

**Response:**
```json
{
  "success": true
}
```

**Status Codes:**
- `200 OK` - Success
- `500 Internal Server Error` - Server error

**Error Response:**
```json
{
  "success": false,
  "error": "Error message"
}
```

---

## Notes

- All endpoints use relative paths starting with `/api/`
- All endpoints support CORS (Access-Control-Allow-Origin: *)
- Error responses follow a consistent format with `success` and `error` fields
- The `/api/sequence` endpoint uses binary data for efficiency
- All endpoints return JSON except `/api/sequence/pointer` which returns plain text
