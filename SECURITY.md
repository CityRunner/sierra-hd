# Security

sierra-hd reads game files that users supply, so malformed game data is
its main attack surface. The parsers are fuzzed (`tests/fuzz/`), and the
whole test suite runs under ASan, UBSan and TSan.

To report a vulnerability, please use GitHub's private vulnerability
reporting for this repository (the "Security" tab, "Report a
vulnerability") rather than a public issue. Include the release or commit,
the platform, and, if possible, an input that reproduces the problem. Do
not attach game files; describe how to modify a copy of the game instead.

Only the latest release and the `main` branch receive fixes.
