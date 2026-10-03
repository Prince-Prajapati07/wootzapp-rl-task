# MiniShop RL Task

This repo contains a small shopping page, a C++17 Chrome DevTools Protocol environment, Python agents, and a log-driven report generator.

## Run

```bash
chmod +x run.sh
./run.sh
```

For a fast smoke run:

```bash
SKIP_TRAIN=1 ./run.sh
```

Dependencies: `g++`, `python3`, `chromium-browser` or `chromium`, OpenSSL development libraries, and Python `matplotlib`.

## What Is Built

- `site/index.html`: plain HTML/JavaScript MiniShop with catalog, product, cart, newsletter, and done screens.
- `env/minishop_env.cpp`: launches headless Chromium and talks to Chrome DevTools Protocol over a raw WebSocket.
- `agents/`: Python subprocess drivers for a random agent and a tabular Q-learning agent.
- `analysis/analyze.py`: reads JSONL logs, computes success rates and confidence intervals, and writes `report.md`.

## Evaluation

`run.sh` builds the environment, trains Q-learning on all 12 item/quantity goals over three seeds, evaluates random and learned agents, evaluates popup rates 0, 0.15, and 0.4, then produces the report and chart.

The environment logs every step as JSONL. The report only uses those logs.

## Limitations

The C++ WebSocket client is intentionally minimal and only implements what CDP needs here. Training uses a small hand-made state, so it is explainable but not general. Delays are disabled during training because they only add wall-clock time; popup behavior remains enabled.
