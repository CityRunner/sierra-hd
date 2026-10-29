# Audit and iteration 2: the customer's task

Given by the customer on 2026-10-29, added to the end of the work list.
Placed after the optional milestones 10 to 12 (customer's decision,
2026-10-29): it starts once each of them is either done or declined at
its own approval. Not started yet. The text below is the customer's,
unchanged.

---

TASK
Audit the codebase, reconcile it with the requirements, and prepare iteration 2.

WORKING RULES
- Work strictly in stages. At every [STOP], halt and wait for an explicit "continue".
- Stages 1–4 are READ-ONLY: do not modify any source files.
- At every [STOP], post a short chat summary: top 5 findings + what you need from me.
- Save all outputs to /review/. Maintain /review/STATUS.md: current stage, decisions made, open items — so work can resume in a new session.
- Use stable IDs across all files: R-xx (requirements), G-xx (gaps), Q-xx (questions), B-xx (backlog items). Cross-reference them.
- Severity scale, used everywhere:
  Critical — broken core functionality, data loss, or blocks iteration 2;
  High — significant deviation from requirements or serious risk;
  Medium — noticeable quality or maintainability issue;
  Low — cosmetic or minor.
- Never present guesses as facts. If you cannot verify something, say so. When in doubt, ask.

STAGE 1a. Codebase map [STOP]
Map modules, entry points, data flows, build system, external dependencies, and current test coverage (what is tested and how).
File: MAP.md

STAGE 1b. Recover requirements [STOP]
Recover the requirements the code actually implements (R-xx). Tag each as:
- FACT — with a file:line reference; or
- HYPOTHESIS — with a confidence level and rationale.
File: REQUIREMENTS_RECOVERED.md

STAGE 2. Reconcile with the original requirements [STOP]
Compare against the original requirements in your context. Classify each item (G-xx, with severity):
- fully implemented;
- partially implemented or deviating;
- not implemented;
- implemented but not required (scope creep);
- contradiction or ambiguity in the requirements themselves.
Reference both code and requirement for each item.
File: GAPS.md

STAGE 3. Grilling session [STOP after each batch]
Ask questions (Q-xx) in batches of 5–10, highest severity first. For each:
- what is unclear or questionable;
- your hypothesis for the answer;
- what changes depending on the answer.
Challenge whether the requirements are justified and whether the results are good — not just details.
If I answer "don't know" or "you decide", propose a default and mark it ASSUMED (unconfirmed).
Record answers in QUESTIONS.md. Continue until all Critical/High questions are closed, then ask whether to move on.

STAGE 4. Improvements and iteration 2 backlog [STOP — wait for approval]
- Improvements (B-xx): defects, tech debt, architecture, performance, maintainability. Each with severity, effort, and risk.
- Additional requirements for iteration 2, each with acceptance criteria.
- Interim refactoring plan, limited to changes that block iteration 2 or significantly reduce risk. Behavior must not change.
Files: BACKLOG_V2.md, REFACTOR_PLAN.md

STAGE 5. Refactoring (only after I approve the plan)
1. Safety net, before any change:
   - If coverage is insufficient, add characterization tests and golden outputs that lock in current behavior. For rendering: render ALL resources (not a sample) to reference images and compare pixel-by-pixel.
   - Golden outputs must be deterministic — verify by generating them twice.
   - Record baselines: build warnings (-Wall -Wextra), sanitizer results (ASan/UBSan), key performance timings.
   - All tests must pass on the original code.
2. Refactor on a separate git branch, in small atomic commits with clear messages. Run the full suite after every commit.
3. Prohibited: changing observable behavior, fixing bugs "while you're at it", going beyond the approved plan. Log any bugs found in BACKLOG_V2.md.
4. Final report: changes made, test/warning/sanitizer/performance results before vs. after, risks found, remaining work.
File: REFACTOR_REPORT.md
