# Claude Code entry

This repository uses shared living-docs style documentation under `docs/`.

## Read order

1. [docs/ARCH_documentation-governance.md](docs/ARCH_documentation-governance.md)
2. [README.md](README.md)
3. [docs/ARCH_repo-overview.md](docs/ARCH_repo-overview.md)
4. [docs/STANDARDS_generated-code-boundaries.md](docs/STANDARDS_generated-code-boundaries.md)
5. Then the task-relevant `GUIDE_`, `REF_`, `LOGIC_`, or `INCIDENT_` document(s)

## Rules

- Shared docs under `docs/` are canonical.
- Do not duplicate shared rules in this file.
- After changing `.ioc`, `MDK-ARM/PSA.uvprojx`, or `uart_safe.c/.h`, run a doc sweep and update the owning shared doc.
