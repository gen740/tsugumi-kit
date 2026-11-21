"""Read the external link requirements for Cargo's static library."""

import json
import sys
from pathlib import Path

target = Path(sys.argv[1])
prefix = "native-static-libs: "
search = []
libraries = None
for line in sys.stdin:
    event = json.loads(line)
    if event["reason"] == "build-script-executed":
        # With --target, paths for host build dependencies live outside this directory.
        if not Path(event["out_dir"]).is_relative_to(target):
            continue
        for path in event["linked_paths"]:
            kind, separator, directory = path.partition("=")
            if not separator:
                search.append(f"-L{path}")
            elif kind in {"all", "native"}:
                search.append(f"-L{directory}")
            elif kind == "framework":
                search.append(f"-F{directory}")
    elif event["reason"] == "compiler-message":
        diagnostic = event["message"]
        if "staticlib" in event["target"]["crate_types"] and diagnostic["message"].startswith(prefix):
            # rustc reports the link order and any required repetitions.
            libraries = diagnostic["message"][len(prefix):].split()
        elif diagnostic["rendered"]:
            sys.stderr.write(diagnostic["rendered"])
    elif event["reason"] == "build-finished" and not event["success"]:
        sys.exit(1)

if libraries is None:
    sys.exit("rustCard: rustc did not report the static library's native link arguments")
for arg in [*dict.fromkeys(search), *libraries]:
    print(arg)
