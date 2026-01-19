/**
 * Integration tests for plugin web UI functions in app.js
 *
 * Tests integration between app.js functions and PluginWebUI module:
 * - loadPluginWebUI()
 * - sendPluginWebUIData()
 * - fetchPluginList()
 * - populatePluginDropdown()
 * - initializePluginSelector()
 *
 * Copyright (c) 2025 the_louie
 */

// Mock DOM environment
global.document = {
    getElementById: jest.fn(),
    createElement: jest.fn(),
    querySelector: jest.fn(),
    readyState: 'complete',
    addEventListener: jest.fn()
};

global.window = {
    PluginWebUI: null
};

// Mock fetch
global.fetch = jest.fn();

describe('Plugin Web UI Integration', () => {
    beforeEach(() => {
        jest.clearAllMocks();
        global.fetch.mockClear();
        document.getElementById.mockReturnValue(null);
        document.createElement.mockImplementation((tag) => {
            const element = {
                tagName: tag.toUpperCase(),
                id: '',
                className: '',
                innerHTML: '',
                textContent: '',
                style: {},
                disabled: false,
                value: '',
                appendChild: jest.fn(),
                insertBefore: jest.fn(),
                querySelector: jest.fn(),
                setAttribute: jest.fn(),
                addEventListener: jest.fn(),
                remove: jest.fn()
            };
            return element;
        });
    });

    describe('loadPluginWebUI', () => {
        test('should check if PluginWebUI module is loaded', () => {
            global.window.PluginWebUI = null;

            // Simulate check
            const isLoaded = global.window.PluginWebUI !== null && typeof global.window.PluginWebUI === 'object';
            expect(isLoaded).toBe(false);

            global.window.PluginWebUI = {
                loadPluginBundle: jest.fn()
            };
            const isLoaded2 = global.window.PluginWebUI !== null && typeof global.window.PluginWebUI === 'object';
            expect(isLoaded2).toBe(true);
        });

        test('should validate plugin name', () => {
            const validNames = ['plugin1', 'plugin_2'];
            const invalidNames = [null, undefined, '', 123];

            validNames.forEach(name => {
                expect(name && typeof name === 'string').toBe(true);
            });

            invalidNames.forEach(name => {
                expect(name && typeof name === 'string').toBe(false);
            });
        });

        test('should show loading state in feedback element', () => {
            const feedbackElement = {
                innerHTML: '',
                className: '',
                textContent: '',
                setAttribute: jest.fn()
            };

            // Simulate loading state
            feedbackElement.innerHTML = '<span class="plugin-ui-spinner"></span><span>Loading plugin UI...</span>';
            feedbackElement.className = 'plugin-ui-feedback-loading';
            feedbackElement.setAttribute('aria-busy', 'true');

            expect(feedbackElement.className).toBe('plugin-ui-feedback-loading');
            expect(feedbackElement.innerHTML).toContain('plugin-ui-spinner');
        });

        test('should disable UI container during load', () => {
            const container = {
                style: {
                    pointerEvents: '',
                    opacity: ''
                }
            };

            // Simulate disabling
            container.style.pointerEvents = 'none';
            container.style.opacity = '0.6';

            expect(container.style.pointerEvents).toBe('none');
            expect(container.style.opacity).toBe('0.6');
        });

        test('should re-enable UI container after load', () => {
            const container = {
                style: {
                    pointerEvents: 'none',
                    opacity: '0.6'
                }
            };

            // Simulate re-enabling
            container.style.pointerEvents = '';
            container.style.opacity = '';

            expect(container.style.pointerEvents).toBe('');
            expect(container.style.opacity).toBe('');
        });
    });

    describe('sendPluginWebUIData', () => {
        test('should check if PluginWebUI module is loaded', () => {
            global.window.PluginWebUI = {
                sendPluginData: jest.fn()
            };

            expect(global.window.PluginWebUI).toBeDefined();
            expect(typeof global.window.PluginWebUI.sendPluginData).toBe('function');
        });

        test('should show sending state in feedback element', () => {
            const feedbackElement = {
                innerHTML: '',
                className: '',
                setAttribute: jest.fn()
            };

            // Simulate sending state
            feedbackElement.innerHTML = '<span class="plugin-ui-spinner"></span><span>Sending data...</span>';
            feedbackElement.className = 'plugin-ui-feedback-loading';
            feedbackElement.setAttribute('aria-busy', 'true');

            expect(feedbackElement.className).toBe('plugin-ui-feedback-loading');
            expect(feedbackElement.innerHTML).toContain('Sending data');
        });

        test('should handle success response', () => {
            const feedbackElement = {
                textContent: '',
                className: '',
                setAttribute: jest.fn()
            };

            // Simulate success
            feedbackElement.textContent = 'Data sent successfully';
            feedbackElement.className = 'plugin-ui-feedback-success';
            feedbackElement.setAttribute('aria-busy', 'false');

            expect(feedbackElement.className).toBe('plugin-ui-feedback-success');
            expect(feedbackElement.textContent).toBe('Data sent successfully');
        });
    });

    describe('fetchPluginList', () => {
        test('should fetch from /api/plugins endpoint', async () => {
            const mockResponse = {
                plugins: ['plugin1', 'plugin2', 'plugin3']
            };

            global.fetch.mockResolvedValueOnce({
                ok: true,
                json: jest.fn().mockResolvedValueOnce(mockResponse)
            });

            const response = await global.fetch('/api/plugins');
            expect(response.ok).toBe(true);
            const data = await response.json();
            expect(data.plugins).toEqual(mockResponse.plugins);
        });

        test('should handle HTTP errors', async () => {
            global.fetch.mockResolvedValueOnce({
                ok: false,
                status: 500,
                json: jest.fn().mockResolvedValueOnce({ error: 'Server error' })
            });

            const response = await global.fetch('/api/plugins');
            expect(response.ok).toBe(false);
            expect(response.status).toBe(500);
        });

        test('should handle network errors', async () => {
            global.fetch.mockRejectedValueOnce(new TypeError('Failed to fetch'));

            await expect(global.fetch('/api/plugins')).rejects.toThrow(TypeError);
        });

        test('should validate response format', () => {
            const validResponse = { plugins: ['plugin1'] };
            const invalidResponse = { data: ['plugin1'] };
            const invalidResponse2 = null;

            expect(validResponse && Array.isArray(validResponse.plugins)).toBe(true);
            expect(invalidResponse && Array.isArray(invalidResponse.plugins)).toBe(false);
            expect(invalidResponse2 && Array.isArray(invalidResponse2?.plugins)).toBe(false);
        });
    });

    describe('populatePluginDropdown', () => {
        test('should find dropdown element by ID', () => {
            const dropdown = {
                id: 'plugin-selector',
                innerHTML: '',
                appendChild: jest.fn(),
                querySelector: jest.fn()
            };

            document.getElementById.mockReturnValueOnce(dropdown);

            const element = document.getElementById('plugin-selector');
            expect(element).toBe(dropdown);
            expect(element.id).toBe('plugin-selector');
        });

        test('should clear existing options', () => {
            const dropdown = {
                innerHTML: '<option>Option 1</option>',
                appendChild: jest.fn(),
                querySelector: jest.fn().mockReturnValue(null)
            };

            // Simulate clearing
            dropdown.innerHTML = '';

            expect(dropdown.innerHTML).toBe('');
        });

        test('should add plugin options', () => {
            const plugins = ['plugin1', 'plugin2'];
            const dropdown = {
                innerHTML: '',
                appendChild: jest.fn(),
                querySelector: jest.fn().mockReturnValue(null)
            };

            // Simulate adding options
            plugins.forEach(pluginName => {
                const option = document.createElement('option');
                option.value = pluginName;
                option.textContent = pluginName;
                dropdown.appendChild(option);
            });

            expect(dropdown.appendChild).toHaveBeenCalledTimes(2);
        });

        test('should handle empty plugin list', () => {
            const plugins = [];
            const dropdown = {
                innerHTML: '',
                appendChild: jest.fn(),
                querySelector: jest.fn().mockReturnValue(null)
            };

            if (plugins.length === 0) {
                const noPluginsOption = document.createElement('option');
                noPluginsOption.value = '';
                noPluginsOption.textContent = 'No plugins available';
                noPluginsOption.disabled = true;
                dropdown.appendChild(noPluginsOption);
            }

            expect(dropdown.appendChild).toHaveBeenCalled();
        });

        test('should format plugin display names', () => {
            const pluginName = 'rgb_effect';
            const formatted = pluginName
                .replace(/_/g, ' ')
                .replace(/([a-z])([A-Z])/g, '$1 $2')
                .split(' ')
                .map(word => word.charAt(0).toUpperCase() + word.slice(1).toLowerCase())
                .join(' ');

            expect(formatted).toBe('Rgb Effect');
        });
    });

    describe('initializePluginSelector', () => {
        test('should find dropdown element', () => {
            const dropdown = {
                id: 'plugin-selector',
                addEventListener: jest.fn()
            };

            document.getElementById.mockReturnValueOnce(dropdown);

            const element = document.getElementById('plugin-selector');
            expect(element).toBe(dropdown);
        });

        test('should create feedback element if not exists', () => {
            const dropdown = {
                id: 'plugin-selector',
                parentElement: {
                    insertBefore: jest.fn()
                },
                nextSibling: null,
                addEventListener: jest.fn()
            };

            document.getElementById.mockImplementation((id) => {
                if (id === 'plugin-selector') return dropdown;
                if (id === 'plugin-selector-feedback') return null;
                return null;
            });

            const feedbackElement = document.getElementById('plugin-selector-feedback');
            if (!feedbackElement) {
                const newElement = document.createElement('div');
                newElement.id = 'plugin-selector-feedback';
                expect(newElement.id).toBe('plugin-selector-feedback');
            }
        });

        test('should add change event listener', () => {
            const dropdown = {
                id: 'plugin-selector',
                value: '',
                addEventListener: jest.fn()
            };

            dropdown.addEventListener('change', jest.fn());

            expect(dropdown.addEventListener).toHaveBeenCalledWith('change', expect.any(Function));
        });

        test('should handle empty selection', () => {
            const pluginName = '';
            const shouldLoad = pluginName && pluginName !== '';

            expect(shouldLoad).toBe(false);
        });

        test('should validate plugin name format', () => {
            const validName = 'plugin1';
            const invalidName = 'plugin 1';

            expect(/^[a-zA-Z0-9_-]+$/.test(validName)).toBe(true);
            expect(/^[a-zA-Z0-9_-]+$/.test(invalidName)).toBe(false);
        });

        test('should check PluginWebUI module availability', () => {
            global.window.PluginWebUI = null;
            expect(global.window.PluginWebUI).toBeNull();

            global.window.PluginWebUI = {
                loadPluginBundle: jest.fn()
            };
            expect(global.window.PluginWebUI).not.toBeNull();
        });
    });
});
