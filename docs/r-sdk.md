# R SDK for the Card ABI

Put `main(input)` in `src/main.R`. The SDK embeds R in the Card's
`lib/main.so`.

```nix
{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/r> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ./_inputs.fbs;
    outputs = ./_outputs.fbs;
  };
in
{
  run = sdk.rCard (common // { src = ./src; });
  api = sdk.rApi common;
  views = sdk.views;
}
```

`rApi` publishes `card.R`, the FlatBuffers schemas, and a typed README
under `share/tsugumi/api/`. Set `entry` to choose another file relative to `src`.

```r
main <- function(input) {
  if (cancelled()) stop("Stopped before writing")
  file <- artifact_dir()
  writeLines(input$text$value, file.path(file$path, "note.txt"))
  list(file = file$artifact)
}
```

The [R starter](../templates/r-write-text/) uses this entry with a FlatBuffer
`text` input and an Artifact `file` output.

The SDK also exposes `sdk.input` and `sdk.artifact` for Input Card
definitions. See the [Card SDK contract](sdk-contract.md).

## Inputs and outputs

Read a connected input with `input$text` or `input[["text"]]`. Use
`optional_input(input, "text")` to handle a disconnected port. The
build-time `params` list contains logical, double, character and list
values. Integer params are exact decimal strings with class
`tsugumi_int64`, which preserves the full signed 64-bit range.

A mixed Merge yields `list(type = "Member", value = table)` after the
SDK matches and verifies the candidate by schema ID. Collect yields
opaque envelopes with `schema`, `bytes` and `artifacts`. Return a
named list of outputs. Omit optional outputs or set them to `NULL`.
A missing required output produces `output_invalid`.

`artifact_dir()` returns an Artifact and a writable directory in
`$artifact` and `$path`. `artifact_upload(path)` copies a file or
directory. `artifact_open(artifact)` returns the granted read-only
directory. Forward an Artifact by returning it in a declared Artifact field.
Declare Artifact fields with `include "tsugumi/artifact.fbs";`. The
host seals created directories when the call returns.

## R values

- FlatBuffers tables and structs use named lists. Numeric vectors and fixed
  arrays arrive as R numeric vectors, including empty and single-element
  values. A vector containing wide integers represented as decimal strings
  stays a list to preserve their precision. Other arrays arrive as lists.
  Return numeric vectors directly. For other arrays, return an unnamed list
  or use `I(vector)`. A missing nullable scalar or child table
  arrives as `NULL`. The generated object API represents absent strings and
  vectors by empty values.
- Known enum values use their schema names. A union uses
  `list(type = "Variant", value = ...)` and an unset union uses `NULL`.
- Integers of magnitude greater than `2^53` travel as decimal strings. Use
  decimal strings to produce these values from R. The floating-point tokens
  `"NaN"`, `"Inf"` and `"-Inf"` arrive as their R floating-point values in
  numeric vectors and as strings in scalar fields. Either form can be returned.
- A `[ubyte]` vector uses a base64 string. Use `jsonlite::base64_dec()` to
  obtain an R raw vector. Either a raw vector or a base64 string can be
  returned.

## Effects

`send(view, bytes)` takes a declared view name and a raw vector.
`send(value)` encodes text, a number, a data frame or a matrix.
The constructors `markdown(text)`, `scalar(value, label)`,
`image(bytes, media_type, alt)`, `points(x, y, z, colors)`,
`line_chart(series)`, `plotly(spec)` and `vega_lite(spec)` select other
views. Local argument and encoding errors raise an R error before calling
the host. If the host cannot accept the message, `send` raises
`"display rejected"`.

`recv(view, timeout_ms)` returns a list with `status` and `bytes`. A
negative timeout waits without a deadline. Status is `"message"`,
`"timeout"` or `"cancelled"`. The SDK frames sends in messages of at
most 16 MiB.

The SDK bundles the `line-chart` and `points` HTML views.
Declare the matching views in your Card's `default.nix` with
`views = sdk.views ++ [ ... ];`.
The bundled `text` view displays values sent with `send("text")`.
See the [view protocol](cpp-sdk.md#display-a-value).

Use `cancelled()` to poll the host and `remaining_ms()` to read its deadline
budget in milliseconds. If the host's query fails, these return `TRUE` and
`0` respectively. The SDK initializes and evaluates R on one dedicated thread
for the lifetime of the worker process. Call R effect functions from that thread.

Native R extensions can look up these C functions with
`R_GetCCallable("tsugumi", name)` on the interpreter thread and invoke the saved
pointers from their own threads:

| Name | C signature |
| --- | --- |
| `send` | `int (const char* view, const uint8_t* bytes, size_t size)` |
| `recv` | `int (const char* view, int64_t timeout_ms, uint8_t** data, size_t* size)` |
| `cancelled` | `bool (void)` |
| `remaining_ms` | `double (void)` |

The native send function returns zero on success. Keep the view name and
bytes alive until it returns. Native threads can send and receive after `main` returns. On a
receive, the return status is 0 for a message, 1 for timeout, 2 for
cancellation and negative for an error. Initialize the output pointer
to null and release the returned bytes with `free()`. Outside an invocation the queries return `true` and `0`.

The builder accepts `tools` for runtime executables. See [Runtime tools](../README.md#runtime-tools).

Extra R packages can be selected with `packages = ps: [ ps.ggplot2 ];`.
The default fontconfig configuration includes DejaVu and IPAexGothic for
graphics devices that use fontconfig. Set `fonts` to supply another list.
The host clears `TMPDIR` between calls. The SDK restores R's `tempdir()`
before each call. Keep files that need to survive across calls under `HOME`.
Uncaught R errors report the source and line of the innermost Card function
when its source reference is available.

Build `checks.<system>.rAbi` to run the R Card through the ABI harness.
