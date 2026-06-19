# Sketching Agent Workflow

This is the coordinator workflow for turning user prompts into alice2 sketches using zSpace.

## Prompt Flow

1. Parse the user prompt.
2. Select participating agents:
   - `zspace_agent` for zSpace API and geometry.
   - `alice2_agent` for sketch lifecycle, display, UI, and renderer pipeline.
   - `code_agent` for reusable helper methods/classes.
   - `build_agent` for compile and error checks.
   - `document_agent` only when triggered by documentation keywords.
3. Update `agents/plan.md`.
4. Wait for user approval unless the user explicitly says to proceed.
5. Implement the approved plan.
6. Run the zSpace build.
7. Fix errors and rebuild until successful.
8. Tell the user to run `alice2\run_with_zspace.bat`.

## Planning Rules

Every plan must include:

- intended sketch behavior
- active agents
- files expected to change
- zSpace APIs likely to be used
- alice2 display calls likely to be used
- build command
- acceptance checks

Keep the plan short enough for the user to approve quickly.

## Build Loop

The build loop is mandatory for code changes:

```bat
alice2\build_with_zspace.bat
```

If there are errors, do not ask the user to debug them. Route them:

- zSpace API/type/topology errors -> `zspace_agent`
- sketch lifecycle/display errors -> `alice2_agent`
- helper method/class errors -> `code_agent`
- linker/build configuration errors -> `build_agent`

Repeat until the build succeeds.

## User Run Step

After a successful build, the user should run:

```bat
alice2\run_with_zspace.bat
```

The user views the result, then continues prompting for changes.

## Refinement Rule

When the user corrects an agent mistake, update the relevant agent guide. Examples:

- mesh winding correction -> `zspace_sketch_generation.md`
- display API preference -> `alice2_agent.md`
- build command behavior -> `build_agent.md`
- documentation trigger behavior -> `document_agent.md`
