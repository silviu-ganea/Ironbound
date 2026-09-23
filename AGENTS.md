# Ironbound — Agent Instructions

Canonical instructions for all AI agents (Codex, Cline, Kilo, or any other coding agent) working in this repository. `.clinerules` and `.kilo/rules/ironbound.md` point here; edit this file, not the pointers.

Ironbound is an Unreal Engine 5.8.2 project. Current C++ source and Git history are the primary sources of implementation truth. Do not create or maintain project-state documents, roadmaps, or architecture documents.

## Default to C++ source

This is an Unreal Engine 5.8.2 project.

For ordinary implementation and debugging tasks, inspect only the relevant C++ source files and their directly related declarations.

Do not read the entire repository, old project plans or long Markdown documents by default.

Do not inspect plugin internals, generated files, Intermediate, Binaries, Saved or Unreal Engine source unless the user specifically requests it or the task demonstrably requires it.

Read only enough code to understand the relevant execution path.

Current source code is authoritative over historical documentation.

## Unreal assets and MCP

For C++ tasks, do not automatically inspect Blueprint graphs, animation assets, DataTables or level assets.

When information from an Unreal asset is genuinely necessary to understand or fix the requested problem, use the existing Unreal MCP tools for targeted, read-only inspection.

Use native MCP operations and the schemas exposed by the connected tools.

Do not invent Unreal Python scripts, Python reflection calls, shell commands, HTTP requests or alternative APIs to explore assets or discover MCP functionality.

Do not dump entire Blueprint graphs or enumerate unrelated assets when a specific component, function, row or property is needed.

If the necessary MCP capability is unavailable, report the exact missing information and ask the user. Do not spend time repairing or reverse-engineering the MCP connection.

Do not edit Blueprints, animation assets, DataTables, maps or other Unreal assets unless the user explicitly authorizes those changes.

## No agent builds or gameplay tests

The user builds and tests the project manually.

Agents must not run Build.bat, UnrealBuildTool, dotnet, Live Coding compilation or any other build command.

Do not launch Unreal Editor, run PIE, attempt an automated gameplay test or troubleshoot the build environment.

Known agent-side build attempts have failed with dotnet startup exceptions even when the same build command works manually. Do not repeat those attempts.

After making C++ changes, report the changed files and any potential compile concerns, then stop.

Wait for the user to provide compiler errors or PIE observations. Fix reported problems in a focused follow-up task.

Never claim compilation or gameplay verification without evidence supplied by the user.

## Small changes and user ownership

Implement only the requested change.

Identify the relevant code path before editing. Prefer the smallest coherent fix.

Do not undertake broad refactors, redesign component boundaries, introduce new systems or change unrelated behavior without explicit authorization.

Preserve the existing separation between controller decisions, pawn combat systems, technique execution, movement and physical behavior.

The combat system must remain usable by either AIController or PlayerController through the same gameplay interfaces.

Do not automatically create tests, plans, documentation or commits.

Do not commit, push, switch branches or discard unrelated changes unless explicitly requested.

For longer tasks, provide brief progress updates identifying the code being inspected, the current finding and the files being changed.

If the task expands beyond its original scope, stop and ask rather than silently continuing.

## Completion

Give a concise summary of the actual changes, the files modified and what the user needs to build or test.

Do not present untested code as working.

Do not automatically update project documentation after a coding task.