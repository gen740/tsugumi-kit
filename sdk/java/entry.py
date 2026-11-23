"""Connect the generated Java adapter to the Card's implementation."""

import json
import re
import sys
from pathlib import Path


def main(src, entry_class, output):
    if not re.fullmatch(r"(?:[A-Za-z_][A-Za-z_0-9]*\.)+[A-Za-z_][A-Za-z_0-9]*", entry_class):
        raise ValueError("javaCard: mainClass must be a package-qualified Java class")
    sources = sorted(path.relative_to(src).as_posix() for path in src.rglob("*.java")
                     if not path.name.startswith("._"))
    arguments = ", ".join(json.dumps(path, ensure_ascii=False) for path in sources)
    output.write_text(f"""package tsugumi;

import java.util.Set;

final class CardEntry {{
    static final Set<String> SOURCES = Set.of({arguments});

    static Card.Output run(Card.Input input) throws Throwable {{
        return {entry_class}.run(input);
    }}
}}
""", encoding="utf-8")


if __name__ == "__main__":
    main(Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3]))
