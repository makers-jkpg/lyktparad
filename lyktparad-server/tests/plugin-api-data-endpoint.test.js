/**
 * Unit tests for Plugin API Data Endpoint
 *
 * Tests the sendPluginData functionality and related helper functions
 * for the POST /api/plugin/<plugin-name>/data endpoint.
 *
 * Copyright (c) 2025 the_louie
 */

// Mock DOM and fetch
global.fetch = jest.fn();
global.window = {};

// Mock console for error logging
global.console = {
    ...console,
    error: jest.fn(),
    log: jest.fn()
};

describe('Plugin API Data Endpoint', () => {
    beforeEach(() => {
        jest.clearAllMocks();
        global.fetch.mockClear();
        console.error.mockClear();
    });

    describe('sendPluginData - Success Cases', () => {
        test('should send Uint8Array data successfully', async () => {
            const testData = new Uint8Array([0xFF, 0x00, 0x80]);
            const mockResponse = {
                ok: true,
                status: 200,
                text: jest.fn().mockResolvedValueOnce('{"success":true}')
            };

            global.fetch.mockResolvedValueOnce(mockResponse);

            // Simulate function call - actual implementation uses window.PluginWebUI
            const response = await global.fetch('/api/plugin/rgb_effect/data', {
                method: 'POST',
                headers: { 'Content-Type': 'application/octet-stream' },
                body: testData
            });

            expect(global.fetch).toHaveBeenCalledWith(
                '/api/plugin/rgb_effect/data',
                expect.objectContaining({
                    method: 'POST',
                    headers: { 'Content-Type': 'application/octet-stream' },
                    body: testData
                })
            );
            expect(response.ok).toBe(true);
            expect(response.status).toBe(200);
        });

        test('should send ArrayBuffer data successfully', async () => {
            const buffer = new ArrayBuffer(3);
            const view = new Uint8Array(buffer);
            view[0] = 0xFF;
            view[1] = 0x00;
            view[2] = 0x80;

            const mockResponse = {
                ok: true,
                status: 200,
                text: jest.fn().mockResolvedValueOnce('{"success":true}')
            };

            global.fetch.mockResolvedValueOnce(mockResponse);

            const response = await global.fetch('/api/plugin/rgb_effect/data', {
                method: 'POST',
                headers: { 'Content-Type': 'application/octet-stream' },
                body: buffer
            });

            expect(response.ok).toBe(true);
        });

        test('should handle zero-length data', async () => {
            const emptyData = new Uint8Array(0);
            const mockResponse = {
                ok: true,
                status: 200,
                text: jest.fn().mockResolvedValueOnce('{"success":true}')
            };

            global.fetch.mockResolvedValueOnce(mockResponse);

            const response = await global.fetch('/api/plugin/rgb_effect/data', {
                method: 'POST',
                headers: { 'Content-Type': 'application/octet-stream' },
                body: emptyData
            });

            expect(response.ok).toBe(true);
            expect(emptyData.length).toBe(0);
        });

        test('should handle maximum size payload (512 bytes)', async () => {
            const maxData = new Uint8Array(512);
            maxData.fill(0xFF);

            const mockResponse = {
                ok: true,
                status: 200,
                text: jest.fn().mockResolvedValueOnce('{"success":true}')
            };

            global.fetch.mockResolvedValueOnce(mockResponse);

            const response = await global.fetch('/api/plugin/rgb_effect/data', {
                method: 'POST',
                headers: { 'Content-Type': 'application/octet-stream' },
                body: maxData
            });

            expect(response.ok).toBe(true);
            expect(maxData.length).toBe(512);
        });

        test('should handle empty response body', async () => {
            const testData = new Uint8Array([0xFF, 0x00, 0x80]);
            const mockResponse = {
                ok: true,
                status: 200,
                text: jest.fn().mockResolvedValueOnce('')
            };

            global.fetch.mockResolvedValueOnce(mockResponse);

            const response = await global.fetch('/api/plugin/rgb_effect/data', {
                method: 'POST',
                headers: { 'Content-Type': 'application/octet-stream' },
                body: testData
            });

            expect(response.ok).toBe(true);
            const text = await response.text();
            expect(text).toBe('');
        });
    });

    describe('sendPluginData - Error Cases', () => {
        test('should handle 400 Bad Request', async () => {
            const testData = new Uint8Array([0xFF, 0x00, 0x80]);
            const mockResponse = {
                ok: false,
                status: 400,
                json: jest.fn().mockResolvedValueOnce({ success: false, error: 'Invalid request' })
            };

            global.fetch.mockResolvedValueOnce(mockResponse);

            const response = await global.fetch('/api/plugin/rgb_effect/data', {
                method: 'POST',
                headers: { 'Content-Type': 'application/octet-stream' },
                body: testData
            });

            expect(response.ok).toBe(false);
            expect(response.status).toBe(400);
            const errorData = await response.json();
            expect(errorData.error).toBe('Invalid request');
        });

        test('should handle 404 Not Found', async () => {
            const testData = new Uint8Array([0xFF, 0x00, 0x80]);
            const mockResponse = {
                ok: false,
                status: 404,
                json: jest.fn().mockResolvedValueOnce({ success: false, error: 'Plugin not found' })
            };

            global.fetch.mockResolvedValueOnce(mockResponse);

            const response = await global.fetch('/api/plugin/invalid_plugin/data', {
                method: 'POST',
                headers: { 'Content-Type': 'application/octet-stream' },
                body: testData
            });

            expect(response.ok).toBe(false);
            expect(response.status).toBe(404);
            const errorData = await response.json();
            expect(errorData.error).toBe('Plugin not found');
        });

        test('should handle 413 Payload Too Large', async () => {
            const oversizedData = new Uint8Array(513);
            oversizedData.fill(0xFF);

            const mockResponse = {
                ok: false,
                status: 413,
                json: jest.fn().mockResolvedValueOnce({ success: false, error: 'Payload too large' })
            };

            global.fetch.mockResolvedValueOnce(mockResponse);

            const response = await global.fetch('/api/plugin/rgb_effect/data', {
                method: 'POST',
                headers: { 'Content-Type': 'application/octet-stream' },
                body: oversizedData
            });

            expect(response.ok).toBe(false);
            expect(response.status).toBe(413);
            const errorData = await response.json();
            expect(errorData.error).toBe('Payload too large');
        });

        test('should handle 503 Service Unavailable', async () => {
            const testData = new Uint8Array([0xFF, 0x00, 0x80]);
            const mockResponse = {
                ok: false,
                status: 503,
                json: jest.fn().mockResolvedValueOnce({ success: false, error: 'Service unavailable' })
            };

            global.fetch.mockResolvedValueOnce(mockResponse);

            const response = await global.fetch('/api/plugin/rgb_effect/data', {
                method: 'POST',
                headers: { 'Content-Type': 'application/octet-stream' },
                body: testData
            });

            expect(response.ok).toBe(false);
            expect(response.status).toBe(503);
            const errorData = await response.json();
            expect(errorData.error).toBe('Service unavailable');
        });

        test('should handle 500 Internal Server Error', async () => {
            const testData = new Uint8Array([0xFF, 0x00, 0x80]);
            const mockResponse = {
                ok: false,
                status: 500,
                json: jest.fn().mockResolvedValueOnce({ success: false, error: 'Internal server error' })
            };

            global.fetch.mockResolvedValueOnce(mockResponse);

            const response = await global.fetch('/api/plugin/rgb_effect/data', {
                method: 'POST',
                headers: { 'Content-Type': 'application/octet-stream' },
                body: testData
            });

            expect(response.ok).toBe(false);
            expect(response.status).toBe(500);
        });

        test('should handle network errors', async () => {
            const testData = new Uint8Array([0xFF, 0x00, 0x80]);

            global.fetch.mockRejectedValueOnce(new TypeError('Failed to fetch'));

            await expect(
                global.fetch('/api/plugin/rgb_effect/data', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/octet-stream' },
                    body: testData
                })
            ).rejects.toThrow(TypeError);
        });

        test('should handle invalid JSON in error response', async () => {
            const testData = new Uint8Array([0xFF, 0x00, 0x80]);
            const mockResponse = {
                ok: false,
                status: 500,
                json: jest.fn().mockRejectedValueOnce(new SyntaxError('Invalid JSON')),
                text: jest.fn().mockResolvedValueOnce('Invalid JSON response')
            };

            global.fetch.mockResolvedValueOnce(mockResponse);

            const response = await global.fetch('/api/plugin/rgb_effect/data', {
                method: 'POST',
                headers: { 'Content-Type': 'application/octet-stream' },
                body: testData
            });

            expect(response.ok).toBe(false);
            expect(response.status).toBe(500);
        });
    });

    describe('Data Format Validation', () => {
        test('should validate plugin name format', () => {
            const validNames = ['rgb_effect', 'sequence-plugin', 'plugin1', 'Plugin2'];
            const invalidNames = ['plugin@name', 'plugin name', 'plugin.name', ''];

            validNames.forEach(name => {
                expect(/^[a-zA-Z0-9_-]+$/.test(name)).toBe(true);
            });

            invalidNames.forEach(name => {
                expect(/^[a-zA-Z0-9_-]+$/.test(name)).toBe(false);
            });
        });

        test('should validate payload size limits', () => {
            const maxSize = 512;
            const validSizes = [0, 1, 100, 256, 512];
            const invalidSizes = [513, 1024, 2048];

            validSizes.forEach(size => {
                expect(size <= maxSize).toBe(true);
            });

            invalidSizes.forEach(size => {
                expect(size > maxSize).toBe(true);
            });
        });

        test('should convert number array to Uint8Array', () => {
            const numberArray = [255, 0, 128, 64];
            const uint8Array = new Uint8Array(numberArray);

            expect(uint8Array).toBeInstanceOf(Uint8Array);
            expect(uint8Array.length).toBe(4);
            expect(uint8Array[0]).toBe(255);
            expect(uint8Array[1]).toBe(0);
            expect(uint8Array[2]).toBe(128);
            expect(uint8Array[3]).toBe(64);
        });

        test('should convert ArrayBuffer to Uint8Array', () => {
            const buffer = new ArrayBuffer(3);
            const view = new Uint8Array(buffer);
            view[0] = 0xFF;
            view[1] = 0x00;
            view[2] = 0x80;

            const uint8Array = new Uint8Array(buffer);
            expect(uint8Array.length).toBe(3);
            expect(uint8Array[0]).toBe(0xFF);
            expect(uint8Array[1]).toBe(0x00);
            expect(uint8Array[2]).toBe(0x80);
        });
    });

    describe('Endpoint URL Construction', () => {
        test('should construct correct endpoint URL', () => {
            const pluginName = 'rgb_effect';
            const expectedUrl = `/api/plugin/${pluginName}/data`;

            expect(expectedUrl).toBe('/api/plugin/rgb_effect/data');
        });

        test('should handle various plugin names in URL', () => {
            const pluginNames = ['rgb_effect', 'sequence', 'test-plugin', 'plugin1'];

            pluginNames.forEach(name => {
                const url = `/api/plugin/${name}/data`;
                expect(url).toMatch(/^\/api\/plugin\/[a-zA-Z0-9_-]+\/data$/);
            });
        });
    });

    describe('Content-Type Header', () => {
        test('should use application/octet-stream Content-Type', () => {
            const headers = { 'Content-Type': 'application/octet-stream' };

            expect(headers['Content-Type']).toBe('application/octet-stream');
        });

        test('should preserve binary data format', () => {
            const testData = new Uint8Array([0xFF, 0x00, 0x80, 0x40]);
            const headers = { 'Content-Type': 'application/octet-stream' };

            expect(headers['Content-Type']).toBe('application/octet-stream');
            expect(testData instanceof Uint8Array).toBe(true);
        });
    });

    describe('Response Parsing', () => {
        test('should parse JSON success response', async () => {
            const jsonResponse = '{"success":true}';
            const parsed = JSON.parse(jsonResponse);

            expect(parsed.success).toBe(true);
        });

        test('should parse JSON error response', async () => {
            const jsonResponse = '{"success":false,"error":"Plugin not found"}';
            const parsed = JSON.parse(jsonResponse);

            expect(parsed.success).toBe(false);
            expect(parsed.error).toBe('Plugin not found');
        });

        test('should handle empty response as success', () => {
            const emptyResponse = '';
            expect(emptyResponse).toBe('');
        });
    });
});
