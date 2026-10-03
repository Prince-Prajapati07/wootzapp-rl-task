#!/usr/bin/env bash
set -euo pipefail

command -v g++ >/dev/null
command -v python3 >/dev/null
if ! command -v chromium-browser >/dev/null && ! command -v chromium >/dev/null; then
  echo "Chromium is required. Install chromium or chromium-browser." >&2
  exit 1
fi

mkdir -p logs analysis
echo "[1/5] Building C++ environment"
make -C env

if [ "${SKIP_TRAIN:-0}" = "1" ]; then
  echo "[2/5] Quick training"
  python3 agents/train.py --quick
  ATTEMPTS=20
else
  echo "[2/5] Training"
  python3 agents/train.py
  ATTEMPTS=200
fi

echo "[3/5] Evaluating random and learning agents"
python3 agents/evaluate.py --agent random --attempts "$ATTEMPTS" --popup-p 0.15 --log logs/random_eval.jsonl
python3 agents/evaluate.py --agent q --attempts "$ATTEMPTS" --popup-p 0.15 --log logs/q_eval_p0.15.jsonl

echo "[4/5] Popup ablations"
python3 agents/evaluate.py --agent q --attempts "$ATTEMPTS" --popup-p 0.0 --log logs/q_eval_p0.0.jsonl
python3 agents/evaluate.py --agent q --attempts "$ATTEMPTS" --popup-p 0.4 --log logs/q_eval_p0.4.jsonl

echo "[5/5] Analysis"
python3 analysis/analyze.py
echo "Done. Report: report.md"
