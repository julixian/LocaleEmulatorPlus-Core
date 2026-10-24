# Repository instructions for AI

## Document locations

Keep instructions intended for AI agents in `ai-docs/`. The root `AGENTS.md` is only an entry point to this file.

Place new AI-generated documentation, including maintenance notes and analysis reports, in `ai-docs/` as requested by the user. Keep existing root guides and READMEs at their current locations unless the user requests a move, and update references whenever relocating a document.

Repository guides describe the current structure, behavior, APIs, and usage. Do not turn them into refactoring diaries, before/after name tables, deletion inventories, or dated test summaries. Keep editor setup out of READMEs. Remove unused internal forwarding headers rather than retaining speculative compatibility paths.

## Public native declarations

Maintain `include/LocaleEmulatorPlus.ixx` as the standalone, copyable third-party ABI declaration for the latest version only; do not include legacy v2 declarations. When changing LEPB, time-zone structures, process information, or public calling signatures, update this declaration and its usage documentation together. Run `tests/run-public-api.ps1` to compare x86/x64 layouts with Core and verify an actual public caller.

## Native support

Internal support entry headers own their namespaces and include their dependencies; do not include them inside an open namespace. Use `lep::HashExportName` for export hashes instead of restoring generated DLL hash tables. When changing support header dependencies, run `tests/run-support-headers.ps1`; preserve the fixed numeric layout baseline checked by `tests/run-support-layout.ps1`. See [native-support.md](native-support.md) for module boundaries and verification.

## Local file inspection

For reading, searching, and finding local files, prefer the FastCtx MCP
server's own tools — `inspect_local_file`, `grep`, and `glob` — over shell
equivalents such as `cat`/`Get-Content`, `rg`/`findstr`/`Select-String`,
and `dir`/`ls -R`.
Use FastCtx file tools directly for local-file operations, including when a
local reference is URI-shaped; pass the equivalent plain absolute filesystem path.
Read only what the task needs. When you need several files, pass them to
one `inspect_local_file` call as files=[{"path": ...}, ...] instead of one
call per file. The last line of every result says `Complete` or
`Partial` — continue only with the exact parameters a `Partial` note
provides.

### Batch replacement

Use FastCtx's `replace` for mechanical find-and-replace across files.
It preserves each file's encoding and line endings, supports dry-run previews,
and rejects concurrent changes before writing. Use apply_patch for generated
content, semantic rewrites, or small local edits.
