> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Support acquisition handoff

The installed playtest `native-trace-20261012-005105-487` recorded 70 hand
claim rejections, one support acquisition and its release on the next sample.
Some failed squeeze attempts had valid contact only 9.7 mm from the support
target, with both hands tracked and no cancellation flags. This rules out
contact distance as the sole cause of the reported failure.

Gameplay trials support with the current input, then acquires ownership using
the renderer's original contact input. A rejected trial previously hard-cancelled
the original support policy and consumed its genuine neutral arm. A later
valid renderer contact could therefore not capture while the squeeze stayed
held. Treat only a rejected new trial as a temporary busy hand: preserve its
neutral arm, publish no hold, and retry on a fresh input. Existing holds still
hard-cancel on lost claim proof. Ownership continues to require the original
fresh contact sample and the gun-hand dependency.

The focused support test covers several rejected trials followed by acceptance
without another release, and confirms proof loss on an established hold still
requires neutral rearming. This source fix has not yet been confirmed in game.
