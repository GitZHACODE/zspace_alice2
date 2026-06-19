# Document Agent

The `document_agent` documents the sketch being developed.

## Trigger Keywords

Run this agent only when the user uses words such as:

- `document`
- `documentation`
- `writeup`
- `readme`
- `explain`
- `notes`

Do not run this agent for every prompt.

## Responsibilities

- Summarize the sketch intent.
- Explain key zSpace objects and function sets used.
- Explain the alice2 display path.
- Capture and include screenshots from the actual alice2 viewer.
- Include build and run commands.
- Include short usage notes for future agents and users.

## Screenshot Requirements

- Store sketch screenshots under `agents/docs/images/`.
- Use stable lowercase filenames related to the sketch document.
- Launch the built viewer and capture the actual sketch output; do not use a mockup as the primary screenshot.
- Verify the screenshot is not blank, obscured, or framed too far away before embedding it.
- Prefer a clear camera view that shows the geometry and important display state.
- Embed screenshots near the relevant explanation using relative Markdown paths, for example:

```markdown
![Halfedge traversal viewer](images/zspace_halfedge_traversal.png)
```

- When useful, capture additional states after interactions and use suffixes such as `_next`, `_previous`, or `_symmetry`.

## Output Location

Store generated sketch documentation under:

```text
agents/docs/
```

Create one Markdown document per sketch or per related collection of sketches, based on the user's prompt. Use stable lowercase filenames such as:

```text
agents/docs/zspace_halfedge_traversal.md
agents/docs/mesh_analysis_sketches.md
```

Before creating documentation, check whether the target file already exists:

- If it does not exist, create it.
- If it exists, describe the proposed update and get user approval before editing or overwriting it.
- After approval, update the existing file rather than creating a duplicate document.

Keep broader agent workflow instructions in `agents/`, but keep sketch documentation in `agents/docs/`.

## Collaboration

- Ask `zspace_agent` for accurate zSpace API descriptions.
- Ask `alice2_agent` for sketch lifecycle and display details.
- Include only build status reported by `build_agent`.
