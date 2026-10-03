import argparse
import os
import random
import time
from common import EnvClient, GOALS, choose_random, run_episode


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--attempts", type=int, default=200)
    p.add_argument("--log", default="logs/random_eval.jsonl")
    p.add_argument("--popup-p", type=float, default=0.15)
    args = p.parse_args()
    os.makedirs(os.path.dirname(args.log), exist_ok=True)
    rng = random.Random(7)
    env = EnvClient(args.log)
    start = time.time()
    wins = 0
    try:
        for ep in range(args.attempts):
            item, qty = GOALS[ep % len(GOALS)]
            ok, _, _ = run_episode(env, item, qty, 1000 + ep, lambda obs: choose_random(obs, rng), args.popup_p)
            wins += ok
    finally:
        env.close()
    print(f"random success {wins}/{args.attempts} in {time.time() - start:.1f}s")


if __name__ == "__main__":
    main()
