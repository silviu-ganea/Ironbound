\# Ironbound Agent Instructions



\## Project



Ironbound is an Unreal Engine 5.8 project.



Treat the current source code and live Unreal assets as authoritative. Documentation may be stale.



Before making changes, inspect the relevant existing implementation and preserve the project's current architecture unless the task explicitly requires changing it.



Do not modify files or Unreal assets when the user asks only for investigation, explanation, or planning.



\## Unreal MCP



The native `unreal-mcp` server is available for inspecting live Unreal state and assets.



Use MCP for information that lives in Unreal rather than inferring it from C++ alone, including:



\- Blueprints

\- Animation Blueprints

\- montages and animation assets

\- DataTables

\- StateTrees / Behavior Trees

\- component defaults

\- level actors

\- asset configuration

\- live Unreal state



\### MCP discovery



Prefer narrow discovery.



When you know the relevant Unreal service, use:



`discover\_python\_class("unreal.<Service>", method\_filter="<narrow\_snake\_case\_filter>")`



Use a specific snake\_case substring such as:



`parent\_class`

`graph\_summary`

`components`

`variables`



Do not use broad `describe\_toolset` calls by default. Large toolsets such as `VibeUE.BlueprintService` can produce extremely large results that the client may truncate or offload.



Use `list\_toolsets` when you need to discover which service contains a capability.



Use `describe\_toolset` only when narrow discovery is insufficient.



Never guess MCP method names, argument names, or schemas.



If discovery or an MCP call fails, do not replace it with shell commands, PowerShell, HTTP requests, filesystem searches, Python execution, or subagents merely to reverse-engineer the MCP interface. Report the failure or use another native MCP discovery operation.



Avoid `execute\_python\_code` when a purpose-built MCP operation exists.



\## Blueprint inspection



Inspect Blueprints incrementally.



Prefer summaries and targeted queries over dumping entire graphs.



Inspect only the graphs, nodes, variables, components, or defaults relevant to the current problem.



Do not infer the live Blueprint configuration solely from C++ source.



\## C++ work



Understand the existing component responsibilities before editing.



Make the smallest coherent change that solves the identified problem.



Do not perform unrelated refactors.



Build:



`IronboundEditor Win64 Development`



If Unreal Live Coding prevents a normal build, report that separately. Do not describe a Live Coding lock as a C++ compilation failure.



\## Investigation discipline



For debugging:



1\. Establish the observed behavior.

2\. Inspect the relevant C++.

3\. Inspect relevant Unreal assets through MCP when necessary.

4\. Identify the specific failure mechanism.

5\. Propose the smallest coherent fix.

6\. Modify only after the requested investigation/planning stage is complete.



Distinguish clearly between:



\- observed facts

\- conclusions supported by inspected code/assets

\- hypotheses

\- proposed changes



Do not present guesses as observations.



\## Efficiency



Minimize unnecessary context and tool calls.



Do not repeatedly read files that have already been inspected unless they changed.



Do not dump entire large files, Blueprint graphs, MCP schemas, or toolsets when a targeted query is sufficient.



Do not spawn subagents for simple searches or tool-output processing.



Stop when the requested task is complete.



\## Communication



Be concise and concrete.



When reporting a problem, identify the specific code, asset, setting, or execution path responsible when evidence supports it.



Do not pad responses with generic Unreal explanations when the question concerns the concrete Ironbound implementation.

