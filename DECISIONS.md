# Decisions

## State

The learning state is `(screen, goal_item, goal_qty, focus_item, focus_qty)`. It is small enough for a table and captures the main task facts: where the agent is, what it must buy, and what item/quantity is currently in focus.

For unseen or tied Q-table rows, the learning agent uses a simple fallback based on the current visible buttons and goal. Learned Q-values override it once they differ. This avoids a brittle all-zero table choosing `click(0)` forever.

## Actions

The action set is `click(0)` through `click(5)` plus `wait`. Invalid clicks are allowed by the agent interface but become no-ops with the step penalty. Real clicks are sent through CDP mouse events at observed button centers.

## Reward

Correct order gives `+1.0`, wrong order gives `-1.0`, and every unfinished step gives `-0.01`. The small step cost encourages shorter paths without pretending partial progress is success.

## Episode End

An episode ends when any order is placed or when 20 steps are reached. CDP failures are treated as truncation with error info.

## Skipped

I skipped parallel environments and packaged one tiny Harbor-format task instead. Training disables delay probability because button delay only burns wall-clock time and does not change the decision problem.

## More Time

I would improve the state extractor so it reads the visible product/cart text directly instead of using the goal as a simple proxy, and I would add a richer timing breakdown inside the C++ environment.
