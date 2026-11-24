# Templates

These examples target **the Card ABI (unreleased)**. Their C++ Cards require the
upcoming Card ABI integration in Tsugumi.

| Template | What you can copy |
| --- | --- |
| [Filter channel counts](filter-counts/) | A C++ Card that filters a CSV Artifact, forwards the original and shows a live table preview. |
| [Clear a Dawn view](dawn-view/) | A C++ Card that sets a WebGPU canvas colour and logs click positions. |
| [Convert a temperature](temperature-conversion/) | A C++ Card that converts its Celsius parameter to Fahrenheit. |
| [Normalize text with Go](go-normalize-text/) | A Go Card with a text parameter that collapses whitespace and returns a one-file Artifact. |
| [Number lines with Rust](rust-number-lines/) | A Rust Card with a text parameter that numbers the lines and returns a one-file Artifact. |
| [Write text with Python](python-write-text/) | A Python Card with a text parameter that returns a text Artifact. |
| [Write text with Julia](julia-write-text/) | A Julia Card with a text parameter that writes text into an Artifact. |
| [Write text with R](r-write-text/) | An R Card that writes its text parameter into an Artifact. |
| [Write text with Ruby](ruby-write-text/) | A Ruby Card with a text parameter that writes text into an Artifact. |
| [Write text with Java](java-write-text/) | A Java Card with a text parameter that displays the text and returns a one-file Artifact. |
| [Write a note with C#](csharp-write-note/) | A C# Card that writes a parameter to an Artifact. |
| [Write a note with JavaScript](js-write-note/) | A Node.js Card that writes a parameter to a text Artifact and displays its byte count. |

Every Card has a complete `_card.json` for its type and a `README.md` with `title` and `description` front matter. Project templates carry `tsugumi.json` with the target `tsugumiVersion`, and `tsugumi.lock` with the nixpkgs
and SDK source pins. Card-only templates use the destination project's
`nixPath`. See the [template guide](../docs/templates.md) for the format and Git import fields.
