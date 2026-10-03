import argparse
import json
import os
import random
import time
from common import EnvClient, GOALS, choose_random, q_action, run_episode


def load_q(path):
    with open(path) as f:
        return json.load(f)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--agent", choices=["random", "q"], default="q")
    p.add_argument("--attempts", type=int, default=200)
    p.add_argument("--popup-p", type=float, default=0.15)
    p.add_argument("--log", default=None)
    p.add_argument("--q", default="agents/q_table.json")
    args = p.parse_args()
    log = args.log or f"logs/{args.agent}_eval_p{args.popup_p}.jsonl"
    os.makedirs(os.path.dirname(log), exist_ok=True)
    rng = random.Random(99)
    q = load_q(args.q) if args.agent == "q" else {}
    env = EnvClient(log)
    wins = 0
    start = time.time()
    try:
        for ep in range(args.attempts):
            item, qty = GOALS[ep % len(GOALS)]
            seed = 90000 + ep
            if args.agent == "random":
                policy = lambda obs: choose_random(obs, rng)
            else:
                def policy(obs):
                    return q_action(q, obs)
            ok, _, _ = run_episode(env, item, qty, seed, policy, popup_p=args.popup_p)
            wins += ok
    finally:
        env.close()
    print(f"{args.agent} popup_p={args.popup_p} success {wins}/{args.attempts} in {time.time() - start:.1f}s")


if __name__ == "__main__":
    main()
