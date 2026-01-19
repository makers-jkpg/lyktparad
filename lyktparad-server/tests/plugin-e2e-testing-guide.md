# Plugin Web UI End-to-End Testing Guide

This document provides manual testing procedures for the plugin web UI functionality.

**Copyright (c) 2025 the_louie**

## Test Setup Requirements

1. **Backend Server**: ESP32 device running the embedded webserver OR external Node.js server
2. **Frontend**: Web browser (Chrome, Firefox, or Safari)
3. **Network**: Device and browser must be on the same network
4. **Plugins**: At least one plugin with web UI should be available on the device

## Test Scenarios

### Scenario 1: Plugin List Fetching

**Objective**: Verify that the plugin list is fetched and displayed correctly.

**Steps**:
1. Open the web UI in a browser
2. Navigate to the Plugins tab
3. Observe the plugin dropdown in the page header

**Expected Behavior**:
- Dropdown should be populated with available plugin names
- Plugin names should be formatted (underscores replaced with spaces, proper capitalization)
- If no plugins are available, dropdown should show "No plugins available" (disabled)
- If fetch fails, dropdown should show error message

**Error Scenarios to Test**:
- Network disconnected: Dropdown should show error, page should still function
- Server unreachable: Dropdown should show error message
- Invalid response format: Error should be logged to console, dropdown should show error

**Verification**:
- Check browser console for errors
- Verify dropdown options match available plugins
- Verify error messages are user-friendly

---

### Scenario 2: Plugin Selection and UI Loading

**Objective**: Verify that selecting a plugin loads its UI bundle correctly.

**Steps**:
1. Select a plugin from the dropdown
2. Observe the loading state
3. Wait for plugin UI to load

**Expected Behavior**:
- Loading spinner should appear in feedback element
- "Loading plugin UI..." message should be displayed
- Plugin UI container should be disabled (opacity reduced, pointer-events: none)
- After loading, plugin HTML/CSS/JS should be injected
- Success message should appear briefly (2 seconds)
- UI container should be re-enabled

**Error Scenarios to Test**:
- Plugin not found (404): Error message should be displayed, dropdown should remain functional
- Invalid bundle format: Error should be displayed
- Network error during load: Error should be displayed with retry option
- Plugin has no web UI: Appropriate message should be shown

**Verification**:
- Check that plugin HTML is visible in the container
- Check that plugin CSS styles are applied
- Check that plugin JavaScript functions are available
- Verify no console errors
- Verify accessibility (ARIA attributes)

---

### Scenario 3: Plugin Data Sending

**Objective**: Verify that plugin data can be sent to the device.

**Steps**:
1. Load a plugin UI that has data sending functionality
2. Interact with the plugin UI to send data (e.g., change RGB values)
3. Observe the sending state and response

**Expected Behavior**:
- Sending spinner should appear
- "Sending data..." message should be displayed
- Data should be sent as raw bytes (application/octet-stream)
- Success message should appear briefly (2 seconds)
- Plugin should receive and process the data

**Error Scenarios to Test**:
- Payload too large (>512 bytes): Error 413 should be displayed
- Plugin not found: Error 404 should be displayed
- Network error: Error message with retry option
- Invalid data format: Validation error should be displayed

**Verification**:
- Check browser network tab for POST request to `/api/plugin/:name/data`
- Verify Content-Type header is `application/octet-stream`
- Verify payload size is within limits
- Check that plugin receives data correctly (verify on device)

---

### Scenario 4: Multiple Plugin Loads

**Objective**: Verify that switching between plugins works correctly.

**Steps**:
1. Select plugin A from dropdown
2. Wait for plugin A UI to load
3. Select plugin B from dropdown
4. Wait for plugin B UI to load
5. Switch back to plugin A

**Expected Behavior**:
- Previous plugin UI should be cleared before loading new one
- Previous plugin CSS should be removed
- Previous plugin JavaScript should not interfere
- Each plugin should load independently
- No memory leaks or leftover event listeners

**Verification**:
- Check that only current plugin UI is visible
- Check that previous plugin styles are removed from DOM
- Check browser console for errors
- Verify no duplicate event listeners

---

### Scenario 5: Error Recovery

**Objective**: Verify that errors are handled gracefully and recovery is possible.

**Steps**:
1. Cause an error (e.g., disconnect network, select invalid plugin)
2. Observe error message
3. Attempt recovery (retry, reconnect, select valid plugin)

**Expected Behavior**:
- Error messages should be clear and actionable
- Retry buttons should appear where appropriate
- UI should remain functional after errors
- User should be able to recover without page refresh

**Error Types to Test**:
- Network errors: Should show retry option
- HTTP errors (404, 500): Should show appropriate message
- Validation errors: Should show what was wrong
- Timeout errors: Should show timeout message

**Verification**:
- Error messages are user-friendly
- Retry functionality works
- UI state is properly reset after errors
- No broken state after error recovery

---

### Scenario 6: UI State Management

**Objective**: Verify that UI states (loading, success, error) are managed correctly.

**Steps**:
1. Observe UI during various operations (loading, success, error)
2. Check that states transition correctly
3. Verify that states don't conflict

**Expected Behavior**:
- Loading state should show spinner and disable interactions
- Success state should show message briefly then clear
- Error state should show message with retry option
- States should not overlap or conflict
- Accessibility attributes should be updated (aria-busy, aria-live)

**Verification**:
- Check CSS classes are applied correctly
- Check ARIA attributes are updated
- Verify spinner appears/disappears correctly
- Verify no visual glitches during state transitions

---

### Scenario 7: Integration with Existing Features

**Objective**: Verify that plugin UI doesn't break existing functionality.

**Steps**:
1. Use existing features (mesh control, sequence, etc.)
2. Load plugin UI
3. Use both existing and plugin features together

**Expected Behavior**:
- Existing features should continue to work
- Plugin UI should not interfere with existing UI
- Tab navigation should work correctly
- No CSS conflicts
- No JavaScript conflicts

**Verification**:
- All existing features work as before
- No layout breaks
- No JavaScript errors
- No style conflicts

---

## Test Checklist

Use this checklist to ensure all scenarios are tested:

- [ ] Plugin list fetching works
- [ ] Plugin list fetching handles errors
- [ ] Plugin selection works
- [ ] Plugin UI loading works
- [ ] Plugin UI loading handles errors
- [ ] Plugin data sending works
- [ ] Plugin data sending handles errors
- [ ] Multiple plugin loads work correctly
- [ ] Previous plugin UI is cleared
- [ ] Error recovery works
- [ ] UI states are managed correctly
- [ ] Loading spinner appears/disappears
- [ ] Success messages appear/disappear
- [ ] Error messages are clear
- [ ] Retry functionality works
- [ ] Integration with existing features works
- [ ] No console errors
- [ ] No memory leaks
- [ ] Accessibility attributes are correct
- [ ] Responsive design works

## Troubleshooting

### Plugin list not loading
- Check network connection
- Check browser console for errors
- Verify `/api/plugins` endpoint is accessible
- Check server logs

### Plugin UI not loading
- Check browser console for errors
- Verify plugin has web UI bundle
- Check `/api/plugin/:name/bundle` endpoint
- Verify bundle format is valid JSON

### Data sending fails
- Check payload size (max 512 bytes)
- Verify plugin name is correct
- Check network connection
- Verify Content-Type header

### UI conflicts
- Check for CSS class name conflicts
- Verify JavaScript namespace usage
- Check for duplicate event listeners
- Verify DOM element IDs are unique

## Notes

- All tests should be performed in multiple browsers (Chrome, Firefox, Safari)
- Test on different screen sizes (desktop, tablet, mobile)
- Test with different network conditions (fast, slow, intermittent)
- Test with different numbers of plugins (0, 1, many)
- Document any issues found during testing
