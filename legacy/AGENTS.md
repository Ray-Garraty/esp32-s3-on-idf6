# 📘 AGENTS.md — mandatory general dev rules for AI-agents.

⚙️ General Rules

- Never break existing functionality or business logic.
- Never delete/move code you don't understand — investigate first.
- Never invent methods/hooks/APIs. Always verify against official docs or MCP context7. Check library versions before using specific hooks/props.

💬 User Interaction

- Always answer user questions immediately (!), clearly, and concisely.
- Use Markdown, code blocks with language tags, clear headers. Avoid vague phrases like "here's the code".
- Show only changed fragments with file paths and line numbers — never output full files >50 lines unless essential.
- If you make a mistake: admit it honestly, explain briefly, provide a fix — no fluff.

🔒 Git — Strictly Read-Only
✅ Allowed: git status, log, diff, show, branch -a, ls-files, check-ignore
❌ Forbidden without explicit user consent: commit, add, reset, checkout, rebase, amend, push, stash, branch/tag creation, git rm

🛠️ Code Style & Development

- Core principles: DRY, KISS, YAGNI, pragmatic SRP.
- Balance over dogma. Favor readability and maintainability over formal adherence to textbook principles. Don't fragment code unless it genuinely improves clarity or reduces coupling.
- Prefer cohesive units that are easy to reason about over perfectly separated micro-modules. A 200-line function doing one logical thing is often better than 5 functions with obscure names and excessive indirection
- Constants: Extract magic numbers to config/module top (except well-known constants like 1000 ms, or test-only values).

Linting:

- .c/.cpp: Clang-Tidy
  Temp debug code may skip linting. Never suppress lint errors without user approval.
  For risky refactors: leave // Technical debt: refactor needed.

📚 External Library Skills

This project defines Opencode skills for key external libraries. When you start working on a module, the relevant skill will be loaded automatically (based on keyword matching in your query). The skill tells you exactly which online docs to read and what pitfalls to avoid.

| Skill | Triggers | Docs source |
|---|---|---|
| `fastaccel-stepper` | stepper, FastAccelStepper, stepper.h, stepper.cpp, TMC2209 | context7 + GitHub |
| `arduino-json` | ArduinoJson, JsonDocument, command.cpp, status.cpp, JSON | arduinojson.org/v7/api |
| `tmc-stepper` | TMC2209 driver, stepper_drv, StallGuard, stealthChop, registers | context7 + GitHub |
| `esp-async-webserver` | webserver, SSE, REST, HTTP, ESPAsyncWebServer, WebUI | context7 + GitHub |

**If a skill is not auto-loaded but the task touches one of these libraries, load it manually via the `skill` tool.**

🔍 Pre-Work Audit
When information is insufficient → never guess or reinvent:

1. Pause coding.
2. Search docs, codebase, legacy code (if any).
3. **If working with an external library, load the corresponding skill first.**
4. Run diagnostic terminal commands.
5. Search web / MCP context7.
6. Ask the user clarifying questions.

🔄 Multi-Step Refactoring

1. Plan first, get user approval before coding.
2. Make changes atomically with intermediate checks (build, lint, tests).
3. Don't modify many files at once.
4. After each stage: verify visible improvement. If none — stop, report status, issues, next steps.
5. Clean up unused temp files/duplicates.

🪟 Windows Environment Notes

- Use taskkill cautiously (risk of killing agent process).
- Always ignore \r in search/comparison.
- Always use LF (\n) line endings when creating/editing files.
- Mandatory after any file change: `dos2unix <filename>`

- **ABSOLUTELY FORBIDDEN to launch background processes that hold COM ports.** Any blocking/serial tool (pio device monitor, serial terminal, etc.) MUST be run with an explicit timeout via the bash tool's `timeout` parameter or via `timeout <seconds>` prefix. Never leave a process occupying COM after exit.

🐍 Python Script Rules

- NEVER use `python -c "..."` inline scripts — bash on Windows mangles quotes/backslashes.
- ALWAYS write Python code to a temp file first, then run it.
  Use `C:\Users\vlbes\AppData\Local\Temp\opencode` for temp scripts.
- Example:
  ```bash
  # Write temp script
  cat > "$TMPDIR/test_serial.py" << 'PYEOF'
  ... python code ...
  PYEOF
  python "$TMPDIR/test_serial.py"
  ```

🪟 Git Bash COM-port monitor command with timeout:
`timeout 30 pio device monitor --baud 115200`
