# Repository Working Conventions

- Preserve Windows CRLF line endings in all text files.
- New text files must use Windows CRLF line endings.
- Ignore everything under `.build/` except `.build/vs2022/`; `.build/release/`
  contains official signed release artifacts and is not part of normal source
  inspection or testing.
- Use `bld.cmd` for builds; it configures the Visual Studio tool paths itself.
