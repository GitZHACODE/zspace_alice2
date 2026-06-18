# alice2 Multi-Agent Workflow

This folder defines the working agreement for agents helping users create alice2 sketches with zSpace geometry.

## Agent Roles

- `zspace_agent.md`: knows the zSpace API, geometry objects, function sets, iterators, IO, and mesh winding conventions.
- `alice2_agent.md`: knows alice2 sketch structure, display pipeline, `scene().draw(...)`, renderer/UI flow, and zSpace display integration.
- `code_agent.md`: creates small reusable methods/classes requested by the sketch, with API help from `zspace_agent`.
- `build_agent.md`: builds the project, reads compiler errors, and drives the fix loop until the build is clean.
- `document_agent.md`: documents the sketch only when explicitly triggered by words such as `document`, `documentation`, `writeup`, `readme`, or `explain`.
- `querying_and_using_zspace_api.md`: local copy of the zSpace API usage guide for examples and method discovery.
- `sketching_agent_workflow.md`: coordinator loop from prompt to approved plan, implementation, build, and user run step.
- `zspace_sketch_generation.md`: zSpace sketch conventions, including mesh winding.
- `docs/`: one document per sketch or user-requested collection of related sketches.

## Required Planning Gate

Every user prompt starts with a plan. Agents write or update `agents/plan.md` with:

- user intent
- participating agents
- proposed files to change
- build command
- acceptance checks
- implementation status

Implementation should start after the plan is approved by the user, unless the user explicitly asks to proceed immediately.

## Required Build Loop

All code-producing agents stay in the loop until `build_agent` reports a successful zSpace build.

Use:

```bat
alice2\build_with_zspace.bat
```

If the build fails:

1. `build_agent` extracts the actual errors.
2. The relevant agent patches the cause.
3. `build_agent` runs the build again.
4. Repeat until there are no build errors.

After a clean build, tell the user to run:

```bat
alice2\run_with_zspace.bat
```

The user can inspect the viewer and continue prompting.

## Refinement Rule

When a prompt reveals a new convention, mistake, or preferred workflow, update the relevant agent file. Keep these files alive and practical rather than one-time documentation.
