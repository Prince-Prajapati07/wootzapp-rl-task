import argparse
import json
import os
import random
import time
from common import EnvClient, GOALS, actions, q_action, state_key, run_episode


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--episodes", type=int, default=120)
    p.add_argument("--quick", action="store_true")
    p.add_argument("--out", default="agents/q_table.json")
    args = p.parse_args()
    if args.quick:
        args.episodes = 12
    os.makedirs("logs", exist_ok=True)
    q = {}
    curves = []
    start = time.time()
    for run, base_seed in enumerate([11, 23, 37]):
        rng = random.Random(base_seed)
        env = EnvClient(f"logs/train_run{run}.jsonl")
        eps = 0.35
        wins = []
        try:
            for ep in range(args.episodes):
                item, qty = rng.choice(GOALS)

                def policy(obs):
                    s = state_key(obs)
                    q.setdefault(s, {a: 0.0 for a in actions()})
                    if rng.random() < eps:
                        return rng.choice(actions())
                    return q_action(q, obs)

                def learn(s, a, nxt, terminal):
                    ns = state_key(nxt)
                    q.setdefault(s, {x: 0.0 for x in actions()})
                    q.setdefault(ns, {x: 0.0 for x in actions()})
                    target = nxt["reward"]
                    if not terminal:
                        target += 0.95 * max(q[ns].values())
                    q[s][a] += 0.3 * (target - q[s][a])

                ok, _, _ = run_episode(env, item, qty, base_seed * 10000 + ep, policy, popup_p=0.15, learn=learn)
                wins.append(1 if ok else 0)
                eps = max(0.05, eps * 0.992)
                window = 20 if not args.quick else 4
                if (ep + 1) % window == 0:
                    curves.append({"run": run, "episode": ep + 1, "success_rate": sum(wins[-window:]) / min(window, len(wins))})
                    print(f"train run {run} episode {ep + 1}/{args.episodes} recent_success={curves[-1]['success_rate']:.2f}", flush=True)
        finally:
            env.close()
    with open(args.out, "w") as f:
        json.dump(q, f, indent=2, sort_keys=True)
    with open("logs/training_curve.jsonl", "w") as f:
        for row in curves:
            f.write(json.dumps(row) + "\n")
    print(f"training finished in {time.time() - start:.1f}s, states={len(q)}")


if __name__ == "__main__":
    main()
