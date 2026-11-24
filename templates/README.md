# Templates

These examples target **the Card ABI (unreleased)**. Their C++ Cards require the
upcoming Card ABI integration in Tsugumi.

| Template | What you can copy |
| --- | --- |
| [Filter channel counts](filter-counts/) | A C++ Card that filters a CSV Artifact, forwards the original and shows a live table preview. |
| [Clear a Dawn view](dawn-view/) | A C++ Card that sets a WebGPU canvas colour and logs click positions. |
| [Convert a temperature](temperature-conversion/) | An Input Card wired to a C++ Card, with a layout fragment for the pair. |
| [Normalize text with Go](go-normalize-text/) | An Input Card wired to a Go Card that collapses whitespace and returns a one-file Artifact. |
| [Number lines with Rust](rust-number-lines/) | An Input Card wired to a Rust Card that numbers the lines and returns a one-file Artifact. |
| [Write text with Python](python-write-text/) | An Input Card wired to a Python Card that returns a text Artifact. |
| [Write text with Julia](julia-write-text/) | An Input Card wired to a Julia Card that writes text into an Artifact. |
| [Write text with R](r-write-text/) | An Input Card wired to an R Card that writes text into an Artifact. |
| [Write text with Ruby](ruby-write-text/) | An Input Card wired to a Ruby Card that writes text into an Artifact. |
| [Write text with Java](java-write-text/) | An Input Card wired to a Java Card that displays the text and returns a one-file Artifact. |
| [Write a note with C#](csharp-write-note/) | A C# Card that writes a parameter to an Artifact. |
| [Write a note with JavaScript](js-write-note/) | A Node.js Card that writes a parameter to a text Artifact and displays its byte count. |

Project templates carry `tsugumi.json` and `tsugumi.lock` with the nixpkgs
and SDK source pins. Card-only templates use the destination project's
`nixPath`. See the [template guide](../docs/templates.md) for the format and Git import fields.
