#!/bin/sh
# Launch the real executable inside the .app bundle. Running the bundle binary
# directly is what gives macOS the bundle identity it needs for notifications.
exec "$(dirname "$0")/hermes_ui.app/Contents/MacOS/hermes_ui" "$@"
