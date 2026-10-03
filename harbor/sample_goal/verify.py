import sys

text = open(sys.argv[1]).read()
if '"orderItem":"blue-mug"' in text and '"orderQty":2' in text and '"done":true' in text:
    print("ok")
else:
    raise SystemExit("goal not verified")
