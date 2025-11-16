# VS Code Configuration

## Setup Instructions

1. Copy the template to create your local C/C++ configuration:

   **Linux/macOS:**

   ```bash
   cp .vscode/c_cpp_properties.json.template .vscode/c_cpp_properties.json
   ```

   **Windows (PowerShell):**

   ```powershell
   Copy-Item .vscode/c_cpp_properties.json.template .vscode/c_cpp_properties.json
   ```

2. Edit `.vscode/c_cpp_properties.json` and replace:
   - `VERSION` with your actual SDK version (e.g., `0.17.4`)
   - The `~` (tilde) automatically expands to your home directory on all platforms

3. Your customized `c_cpp_properties.json` will be ignored by git, so your local changes won't be committed.

## Files

- `c_cpp_properties.json.template` - Template file tracked by git (cross-platform)
- `c_cpp_properties.json` - Your local customization (gitignored)

## Note

If you still see include errors after setup, try:

1. Reload VS Code window (Cmd+Shift+P → "Reload Window")
2. Verify Zephyr is installed at `~/zephyrproject/zephyr`
3. Check SDK path matches your installation
4. Use the `c_cpp_propertioes.json` found in README.md file
