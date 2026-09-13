\# IRONBOUND — Combat Prototype Roadmap



> \*\*Project:\*\* Ironbound  

> \*\*Engine:\*\* Unreal Engine 5.8.2  

> \*\*Repository:\*\* `silviu-ganea/Ironbound`  

> \*\*Main branch:\*\* `main`  

> \*\*Starting point:\*\* Epic Game Animation Sample Project (GASP)



This file is the project's \*\*living technical memory and roadmap\*\*.



The GitHub repository is the source of truth for the current implementation.  

This document should contain the information that is expensive to rediscover:



\- final design goals,

\- current development goals,

\- completed milestones,

\- important technical discoveries,

\- failed/abandoned approaches,

\- known traps,

\- and the next experiments.



\---



\# AI / DEVELOPER INSTRUCTIONS



\## Always keep this file updated



Whenever meaningful project state changes, update this document.



Update it when:



\- a goal is completed,

\- a new goal is added,

\- an experiment confirms or disproves a hypothesis,

\- an important bug is solved,

\- an architecture decision changes,

\- a workaround is discovered,

\- an approach is abandoned,

\- or an important "aha" moment occurs.



When a roadmap item is completed, change:



`\[ ]` → `\[x]`



Do not mark a feature complete until it has actually been tested.



\## Preserve useful discoveries



If solving something required substantial debugging, record the result here.



The purpose is to prevent future developers or AI agents from repeating the same failed investigation.



Do not silently delete useful historical lessons when implementation changes. Keep a short note explaining what was learned and why the old approach was abandoned.



\## Check the repository when possible



When GitHub access is available, inspect the actual repository instead of relying on this document for implementation details.



This file explains \*\*why, status, discoveries, and roadmap\*\*.



The repository explains \*\*what currently exists\*\*.



Note: Unreal `.uasset` / `.umap` files are binary and may still require Unreal inspection or screenshots to understand Blueprint graph logic.



\---



\# FINAL GAME VISION



Ironbound is intended to become a \*\*physics-driven spectator / management combat game\*\*.



The player manages fighters rather than directly controlling every attack and defensive action.



Core principle:



> \*\*Animation represents intended movement. Physics determines what the fighter can actually achieve.\*\*



A fighter should not perfectly reproduce an attack simply because an animation is playing.



Execution should depend on things such as:



\- strength,

\- skill,

\- balance,

\- fatigue,

\- injury,

\- weapon mass,

\- weapon inertia,

\- body position,

\- collisions,

\- and external forces.



Two fighters using the \*\*same attack animation\*\* should be capable of producing very different physical results.



\---



\# TARGET COMBAT MODEL



Long-term flow:



AI chooses action  

↓  

Target animation / pose  

↓  

Physics Control / active ragdoll tries to follow it  

↓  

Stats determine physical capability  

↓  

Weapon/body physics modify the motion  

↓  

Actual attack / block / failure emerges



Important:



Decision ≠ guaranteed execution  

Animation ≠ final motion



The fighter may make the correct decision and still physically fail.



That failure is intended gameplay.



\---



\# LONG-TERM GOALS



\## Fighter capability



Different fighters should physically perform the same intended action differently.



Examples:



\### Strength



Controls how much physical effort the fighter can produce.



Potential effects:



\- weapon acceleration,

\- lifting heavy weapons,

\- resisting impacts,

\- posture recovery.



\### Skill



Controls how accurately and efficiently the fighter follows intended motion.



Potential effects:



\- tracking accuracy,

\- wobble,

\- overshoot,

\- recovery,

\- weapon trajectory precision.



\### Fatigue / injury



Should degrade physical performance rather than merely changing animation speed.



\### Balance



Should influence resistance to knockback and ability to recover posture.



These mappings are still hypotheses and must be experimentally validated.



\---



\# PHYSICAL WEAPONS



Weapons should physically matter.



Relevant properties include:



\- mass,

\- center of mass,

\- length,

\- inertia,

\- collision shape.



A heavy weapon should actually be harder to accelerate, stop, redirect, and recover.



A weak fighter using a heavy weapon should not behave like a strong fighter using the same weapon.



\---



\# PHYSICAL BLOCKING



Long-term blocking should not depend on thousands of paired attack/block animations.



Desired concept:



Detect incoming weapon  

↓  

Predict trajectory  

↓  

Choose interception point  

↓  

Attempt defensive pose  

↓  

Physics Control moves fighter  

↓  

Weapons physically collide  

↓  

Physics determines result



Possible outcomes:



\- successful block,

\- weapon deflection,

\- weak guard collapsing,

\- missed interception,

\- defender losing balance,

\- attacker overpowering the block.



\---



\# CURRENT PROTOTYPE



Current test setup:



\- player-controlled fighter,

\- opponent/test victim,

\- both using swords,

\- GASP movement/camera foundation,

\- player can trigger a sword attack,

\- right arm uses Physics Control,

\- sword is physically simulated and constrained,

\- opponent can physically react to hits,

\- live debug slider controls attack angular strength.



Main character Blueprint:



`SandboxCharacter\_Mover`



\---



\# COMPLETED MILESTONES



\## Project setup



\- \[x] Start from Epic GASP.

\- \[x] Establish working two-fighter prototype.

\- \[x] Rename project to \*\*Ironbound\*\*.

\- \[x] Put project under Git.

\- \[x] Configure Unreal `.gitignore`.

\- \[x] Remove generated/local files from source control.

\- \[x] Upload tracked project successfully to GitHub.

\- \[x] Confirm local `main` matches `origin/main`.

\- \[x] Connect ChatGPT GitHub access to private Ironbound repository.

\- \[x] Create persistent technical roadmap.



\---



\## Combat stance — tested / completed (2026-09-14)

- [x] Validated in Play: stationary guard, normal keyboard/GASP movement, automatic return to guard, and the existing attack montage.
- `SandboxCharacter_Mover_ABP` caches the post-procedural GASP locomotion pose (`Procedural_FullBody`) as `BaseLocomotionPose`.
- Copy `CombatStance` from `SandboxCharacter_Mover` into the AnimBP; calculate `IsCombatStationary` from `Speed2D < 5` using GASP's existing speed value.
- `Sword_ForwardHigh` + stationary selects the full-body `A_Sword_Guard_ForwardHigh` pose. Moving selects `BaseLocomotionPose` with **NO upper-body guard overlay**; stopping while the stance remains active automatically restores the full-body guard. `None` uses normal GASP locomotion.
- Keep `SwordAttackSlot` after stance selection. The existing attack montage still works and its physical execution looks cleaner / less tangled.
- Deliberately abandoned upper-body-only moving guard for now: normal W movement runs/jogs and looks unnatural with the sword held in guard. Revisit only for dedicated combat footwork or AI shifting.
- Retain this session's `BP_FIghterAIController` and reusable character `Attack` event as the AI intent foundation; further AI behavior is deferred until physical strikes are reliable.

\# CURRENT ATTACK TEST



Attack animation:



`A\_AwesomeSwordAnimationsV5\_AttackCombo2\_Stage1\_Complete\_IP`



Montage:



`AM\_TestSword\_CleanSwing1`



Slot:



`DefaultGroup.SwordAttackSlot`



Temporary input:



`Key 1 → play attack`



\---



\# PHYSICS CONTROL



Right-arm Physics Control currently starts at:



`clavicle\_r`



Important current ideas:



Angular Strength  

→ how aggressively the arm follows target animation



Max Torque  

→ currently unlimited at 0  

→ promising candidate for physical effort / strength



The runtime function:



`SetAttackAngularStrength(NewStrength)`



updates the existing right-arm Physics Control live.



Temporary tests:



Key 5 → Angular Strength 15  

Key 6 → Angular Strength 500



5 and 6 are keyboard keys, not strength values.



\---



\# MAJOR VALIDATED RESULT



\## CORE MECHANIC VALIDATED



The same attack animation was tested at different Physics Control angular strengths.



\### Low strength ≈ 15



Observed:



\- arm strongly lagged,

\- sword struggled to rise,

\- fighter barely completed the attack,

\- attack looked physically weak.



\### High strength ≈ 500



Observed:



\- arm tracked target animation much more strongly,

\- sword produced an effective swing,

\- attack reached the opponent,

\- physical contact produced visible reaction.



\### Conclusion



> \*\*Same intended animation + different physical control strength = different physical execution.\*\*



This validates one of Ironbound's central design assumptions.



The game does not necessarily need separate canned "weak" and "strong" attack animations.



\- \[x] Demonstrate weak execution using the same animation.

\- \[x] Demonstrate strong execution using the same animation.

\- \[x] Demonstrate meaningful physical weapon contact.



\---



\# LIVE DEBUG UI



Widget:



`WBP\_AttackStrengthTest`



Current slider:



`Attack Angular Strength`



Range:



`0–500`



It updates Physics Control during PIE.



\- \[x] Runtime Angular Strength setter.

\- \[x] Live slider.

\- \[x] Numeric display.

\- \[x] Verify physical result changes live.



\---



\# IMPORTANT DISCOVERIES / AHA MOMENTS



\## AHA 1 — The source attack animation is not the main problem



The clean attack was previewed with a correctly oriented sword and no physics interference.



Wrist motion and sword trajectory looked correct.



Therefore, major runtime distortion should first be investigated in:



\- Physics Control,

\- constraints,

\- simulated bodies,

\- collision,

\- torque/strength,

\- weapon physics.



Do not immediately return to animation retargeting.



\---



\## AHA 2 — Physics Control must actually be initialized



`Create Controls and Body Modifiers from Limb Bones` originally had no proper execution path.



Custom event:



`InitArmPhysicsControl`



was added and called from BeginPlay.



Without this, runtime Physics Control settings did not have properly created controls to modify.



\---



\## AHA 3 — Sword simulation and constraint state are coupled



Known working configuration:



Sword Simulate Physics = ON  

PhysicsConstraint = active



Broken test:



Sword Simulate Physics = OFF  

PhysicsConstraint = still active



Result:



severe character distortion / "pretzel"



Do not disable sword physics while leaving the current constraint active.



This is a physics/constraint problem, not an animation problem.



\---



\## AHA 4 — Duplicate widget instances caused misleading UI behavior



Symptoms:



\- overlapping values,

\- two slider thumbs.



Cause:



multiple `WBP\_AttackStrengthTest` instances



The widget creation path now checks whether one already exists before creating another.



\---



\## AHA 5 — Runtime Physics Control tuning works



Physics Control parameters can be changed during PIE using runtime setters.



This makes live stat experiments practical.



\---



\# ABANDONED / AVOIDED PATHS



\## Mixamo retargeting



Earlier path:



`RTG\_Mixamo\_To\_GASP`



was abandoned.



The current sword attack works directly with:



`SK\_UEFN\_Mannequin`



Do not restart the old retargeting investigation unless new evidence requires it.



\---



\# HIT REACTION STATUS



Current sword hit logic is prototype-only.



On sword collision with another `SandboxCharacter\_Mover`, the victim currently enables physics below:



`pelvis`



This produces physical reaction.



Important observation:



The opponent's head visibly reacted to a strong sword impact even without explicitly feeding `Normal Impulse` into an `Add Impulse` node.



Current hit/ragdoll logic is \*\*not final combat/damage architecture\*\*.



\---



\# CURRENT ROADMAP



\## Phase 1 — Clean combat prototype level



Completed:



\- \[x] Duplicate the current known-working GASP level.

\- \[x] Create a dedicated combat-development level: `L\_CombatPrototype`.

\- \[x] Remove demo clutter gradually.

\- \[x] Preserve hidden GASP dependencies.

\- \[x] Test after every cleanup batch.



Cleanup completed and Play-tested successfully. Removed Traversal Gym, teleporters, TargetDummy, floor buttons, demo widgets, arrows/signage and related clutter. Intentionally kept the Landscape/hill for future slope, balance and footing tests. The level retains the working combat setup and core environment actors.

Always verify:



\- movement,

\- camera,

\- two fighters,

\- Key 1 attack,

\- sword physics,

\- Physics Control,

\- hit reaction,

\- strength slider.



Do not mass-delete GASP Content assets yet.



\---



\## Phase 2 — Separate tracking from physical strength



Next major experiment:



`Angular Strength vs Max Torque`



Current hypothesis:



Angular Strength → tracking / skill  

Max Torque → physical effort / strength



This has NOT been proven yet.



Tasks:



\- \[ ] Add runtime Max Torque setter.

\- \[ ] Add live Max Torque debug control.

\- \[ ] Test high/low Angular Strength against high/low Max Torque.

\- \[ ] Determine what each parameter actually represents best.

\- \[ ] Record results here.



\---



\## Phase 3 — Weapon mass and inertia



\- \[ ] Test light vs heavy weapons.

\- \[ ] Test weak fighter + heavy weapon.

\- \[ ] Test strong fighter + heavy weapon.

\- \[ ] Investigate center of mass.

\- \[ ] Investigate inertia.

\- \[ ] Test interaction with Max Torque.

\- \[ ] Investigate `Use Acceleration Drive Mode = false`.



Do not change drive mode casually; expect retuning.



\---



\## Phase 4 — Whole-body physical fighter



\- \[ ] Add torso contribution.

\- \[ ] Add left-arm contribution.

\- \[ ] Develop posture control.

\- \[ ] Develop balance.

\- \[ ] Develop impact disturbance.

\- \[ ] Develop recovery from disturbance.

\- \[ ] Decide when partial/full ragdoll should occur.



\---



\## Phase 5 — Physical defense



\- \[ ] Reliable weapon-vs-weapon collision.

\- \[ ] Incoming weapon trajectory estimation.

\- \[ ] Interception prediction.

\- \[ ] Defensive target generation.

\- \[ ] Physical block attempt.

\- \[ ] Strength/skill influence block success.



\---



\## Phase 6 — Combat AI



Eventually AI should control:



\- positioning,

\- distance,

\- attack selection,

\- attack timing,

\- defense timing,

\- trajectory prediction,

\- interception,

\- fatigue awareness,

\- recovery,

\- risk.



The AI chooses intent.



Physics determines execution.



\---



\# DEVELOPMENT RULES



Prefer small experiments that answer one question at a time.



Avoid prematurely building:



\- huge attack libraries,

\- huge block libraries,

\- complex final AI,

\- final stat systems,

\- final damage systems,

\- large-scale GASP asset cleanup.



Before debugging something difficult, check this document first.



If an experiment reveals something useful, update this document immediately.



Even a result such as:



`This approach does not work because...`



is valuable project knowledge.



\---



\# CURRENT NEXT ACTION



Build and test the first stance-based attack from Sword_ForwardHigh, preserving the validated guard / locomotion / montage flow. Next, separate valid strike windows from incidental physical contact and check recovery / self-snags before expanding AI. Phase 2 torque-vs-tracking experiments remain pending.



\---



\# CORE IDENTITY OF IRONBOUND



The target is not:



> sword animations with ragdoll noise added on top.



The target is:



> a fighter whose physical capability determines whether its intended action actually succeeds.



In short:



AI decides what the fighter wants to do.  

Animation describes intended motion.  

Stats determine physical capability.  

Physics determines what actually happens.

