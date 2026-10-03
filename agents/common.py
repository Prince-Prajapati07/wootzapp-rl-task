import json
import os
import random
import subprocess
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ENV_BIN = os.path.join(ROOT, "env", "minishop_env")
SITE = os.path.join(ROOT, "site", "index.html")
ITEMS = ["blue-mug", "red-shirt", "green-cap", "black-book"]
GOALS = [(item, qty) for item in ITEMS for qty in (1, 2, 3)]


class EnvClient:
    def __init__(self, log_path):
        self.proc = subprocess.Popen(
            [ENV_BIN, "--site", SITE, "--log", log_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1,
        )

    def ask(self, msg):
        self.proc.stdin.write(json.dumps(msg) + "\n")
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        if not line:
            raise RuntimeError("environment stopped")
        out = json.loads(line)
        if "error" in out:
            raise RuntimeError(out["error"])
        return out

    def reset(self, item, qty, seed, popup_p=0.15, delay_p=0.0):
        return self.ask({"cmd": "reset", "task": {"item": item, "qty": qty}, "seed": seed, "popup_p": popup_p, "delay_p": delay_p})

    def step(self, action):
        return self.ask({"cmd": "step", "action": action})

    def close(self):
        if self.proc.poll() is None:
            try:
                self.ask({"cmd": "close"})
                self.proc.wait(timeout=5)
            except Exception:
                self.proc.kill()
                self.proc.wait(timeout=5)


def focus_from_obs(obs):
    text = obs.get("visibleText", "")
    screen = obs.get("screen", "unknown")
    item, qty = "none", 0
    if screen == "product":
        for candidate in ITEMS:
            if candidate.replace("-", " ").title() in text or candidate in text:
                item = candidate
        if "Quantity:" in text:
            try:
                qty = int(text.split("Quantity:")[1].strip()[0])
            except Exception:
                qty = 1
    if screen == "cart":
        cart_text = text.split("Cart", 1)[1] if "Cart" in text else text
        for candidate in ITEMS:
            if candidate in cart_text:
                item = candidate
        for n in (1, 2, 3):
            if f"x{n}" in cart_text:
                qty = n
    return item, qty


def state_key(obs):
    goal = obs.get("goal", "none x0")
    parts = goal.split(" x")
    focus_item, focus_qty = focus_from_obs(obs)
    return "|".join([obs.get("screen", "unknown"), parts[0], parts[1] if len(parts) > 1 else "0", focus_item, str(focus_qty)])


def actions():
    return [f"click({i})" for i in range(6)] + ["wait"]


def heuristic_action(obs):
    buttons = obs.get("buttons", [])
    clickable = [b for b in buttons if b.get("clickable")]
    if not clickable:
        return "wait"
    for b in clickable:
        if b["text"] == "Dismiss":
            return f"click({b['i']})"
    goal = obs.get("goal", "blue-mug x1")
    goal_item = goal.split(" x")[0]
    goal_qty = int(goal.split("x")[-1])
    screen = obs.get("screen")
    if screen == "catalog":
        idx = ITEMS.index(goal_item)
        if idx < len(buttons) and buttons[idx].get("clickable"):
            return f"click({idx})"
    focus_item, focus_qty = focus_from_obs(obs)
    if screen == "product":
        if focus_item != goal_item:
            for b in clickable:
                if b["text"] == "Back":
                    return f"click({b['i']})"
        if focus_qty < goal_qty:
            for b in clickable:
                if b["text"] == "+":
                    return f"click({b['i']})"
        if focus_qty > goal_qty:
            for b in clickable:
                if b["text"] == "-":
                    return f"click({b['i']})"
        for b in clickable:
            if b["text"] == "Add to cart":
                return f"click({b['i']})"
    if screen == "cart":
        if focus_item == goal_item and focus_qty == goal_qty:
            for b in clickable:
                if b["text"] == "Checkout":
                    return f"click({b['i']})"
        for b in clickable:
            if b["text"] == "Clear cart":
                return f"click({b['i']})"
    for b in clickable:
        if b["text"] == "Back":
            return f"click({b['i']})"
    return f"click({clickable[0]['i']})"


def q_action(q, obs):
    s = state_key(obs)
    vals = q.get(s)
    allowed = valid_actions(obs)
    clickable = [a for a in allowed if a != "wait"]
    if clickable:
        allowed = clickable
    if not vals:
        return heuristic_action(obs)
    best = max(allowed, key=lambda a: vals.get(a, 0.0))
    if vals.get(best, 0.0) <= 0.0:
        return heuristic_action(obs)
    return best


def valid_actions(obs):
    acts = [f"click({b['i']})" for b in obs.get("buttons", []) if b.get("clickable")]
    acts.append("wait")
    return acts


def choose_random(obs, rng):
    acts = valid_actions(obs)
    if rng.random() < 0.15:
        return "wait"
    clicks = [a for a in acts if a != "wait"]
    return rng.choice(clicks or acts)


def success(obs):
    if not obs.get("orderPlaced"):
        return False
    return f"{obs.get('orderItem')} x{obs.get('orderQty')}" == obs.get("goal")


def run_episode(env, item, qty, seed, policy, popup_p=0.15, delay_p=0.0, learn=None):
    obs = env.reset(item, qty, seed, popup_p, delay_p)
    total = 0.0
    for _ in range(20):
        s = state_key(obs)
        action = policy(obs)
        nxt = env.step(action)
        total += nxt.get("reward", 0.0)
        if learn:
            learn(s, action, nxt, nxt.get("done") or nxt.get("truncated"))
        obs = nxt
        if obs.get("done") or obs.get("truncated"):
            break
    return success(obs), total, obs
