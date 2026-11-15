# Slice doc plan

How a shipped slice's documentation is brought up to date. The run loop's doc phase executes this
document: one pass, diff-based over the whole slice, which reconciles what is written with what
the slice changed. It is not a rewrite.

## The surfaces, in order

**1. `docs/firmware-design.md`, the design.** Update it when the slice changed a decision it
records: the layout of `main/`, the tasks, the backend client or the event stream, how the
backend's address arrives, the memory budget, the fonts, or a constraint section 4 puts on the
API. Correct the decision in place, in the note's own style. A slice that only builds on a
decision changes nothing here.

**2. `docs/change-discipline.md`, the rules.** Only when the slice changed a standing rule, such as
a new door into `main/ui/` or a new kind of data checked at the boundary.

**3. `docs/architecture/architecture.yaml`, the architecture model.** Update it when the slice
changed what the firmware depends on, such as a new backend or service it talks to. The edge to
SonosController's API is added once SonosController publishes its `svc:` element. Its address
comes from IoT Support's device configuration, so it carries no `boundBy`. `kc project lint`
validates the file.

**4. The doors of `main/ui/`.** `Model.h` and `UiHost.h` say in their comments what they hold.
Keep those comments true when the slice adds to them.

**5. `CLAUDE.md`.** Only when the slice changed something an agent must know every turn. It stays
one screen: a fact is stated once, and detail goes to a `docs/` topic doc with a pointer.

## What does not belong here

- **Slice documents.** The slice's own `slice.md`, `plan.md` and close-out are not doc surfaces.
- **Invented structure.** No README, index or topic doc the repo does not have, unless the slice
  created the thing it documents.
- **Aspirational claims.** Every statement traces to the code in the diff. What the slice left
  owed to the operator, such as a check on a display, is written as owed.
- **Tracker references.** Permanent docs state the world in their own words; an issue id belongs
  in commit messages and slice documents only.
- **Addresses.** No hostname or address of the operator's network: this is a public repo.

## When there is little to do

A slice that changed no decision, rule, dependency or door owes nothing here, and saying so
plainly is the correct outcome. Do not invent doc work to fill the phase.
