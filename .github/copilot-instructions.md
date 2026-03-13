# Copilot Instructions

## Project Guidelines
- When troubleshooting this app, avoid changing console logging assumptions without stronger evidence; the TUI was working before the latest logging redirection change.
- Optimize the app for EGLC-specific trading edge by using the highest same-day METAR observation so far over the hourly source for 'Observed so far'.
- Ensure the TUI uses the full fullscreen layout and fully utilizes available fullscreen space rather than stacking key panels vertically below the hourly schedule. Only the 24-hour charts should be centered in fullscreen, and previous centering changes should not affect non-hourly sections.