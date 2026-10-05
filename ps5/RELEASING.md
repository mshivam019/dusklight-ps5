# Release checklist

1. Keep game dumps, downloaded textures, saves, captures and credentials outside Git.
2. Check dependency pins resolve, commit the port, and run build.py and validate.py.
3. Package the title and corresponding source. Check both ZIPs, notices and SHA256SUMS.
4. Run the packaged executable on the console and record only what was actually tested.
5. Tag the exact built commit with a PS5-prefixed tag, preserving upstream tags.
6. Upload the Windows/Linux ZIPs, source archives, source inventory and SHA256SUMS.
7. Download the published ZIPs again and compare their hashes and contents.

The current repo and releases stay private until the maintainer changes visibility.
Do not replace a released artifact in place. Use a new version for changed binaries.
