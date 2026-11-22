# R SDK for Card ABI 9, revision 2

Put `main(input)` in `src/main.R`. The SDK embeds R in the Card's
`lib/main.so`.

```nix
{ pkgs, params, inputs, schemas }:
let
  sdk = inputs.tsugumi.rSdk;
in
{
  run = sdk.rCard {
    inherit schemas;
    src = ./src;
  };
  views = sdk.views;
}
```

The builder uses the current `_inputs.fbs`, `_outputs.fbs`, and
`_params.fbs` in `schemas`. The Card editor's SDK section and the build
output's `share/tsugumi/api/` expose `card.R`, the schemas, and
a README describing the current port and parameter types. Set `entry` to
choose another file relative to `src`.

```r
main <- function(input) {
  if (cancelled()) stop("Stopped before writing")
  file <- "note.txt"
  writeLines(input$text$value, file)
  list(file = file)
}
```

The [R starter](../templates/r-write-text/) uses this entry with a FlatBuffer
`text` input and a Path `file` output.

## Inputs and outputs

Read a needed input with `input$text` or `input[["text"]]`. When it is absent,
including when the port is disconnected, the SDK reports `input_missing`
and names the port. Use `optional_input(input, "text")` to get the optional
value or `NULL`. Read parameters through `input$params`. A port named
`params` is read through `input[["params"]]` or
`optional_input(input, "params")`.

Return a named list keyed by output port name. Omit an optional output or set
it to `NULL`. A missing required output produces `output_invalid`. Paths are
character strings. Finish writing and close new files or directories under
the call's working directory before returning their paths. Return the input
path unchanged to forward an input.

For a mixed Merge, the adapter verifies the nested buffer and decodes the
selected member table. Read the member name from `arrival$type` and its
table from `arrival$value`. Use `list(type = "Temperature", value = table)`
to return a member on a Merge output. The generated README lists the member
names for each port.

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

The SDK at `lib.${system}.rSdk` bundles the `line-chart` and `points` HTML views.
Declare the matching views in your Card's `default.nix` with
`views = sdk.views ++ [ ... ];`.
For `send("text")`, include the `text` view from `inputs.tsugumi.cppSdk.views`.
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
| `cancelled` | `bool (void)` |
| `remaining_ms` | `double (void)` |

The native send function returns zero on success. Keep the view name and
bytes alive until it returns. Native threads can send after `main` returns. Outside an invocation the queries return `true` and `0`.

The builder accepts `tools` for runtime executables. See [Runtime tools](../README.md#runtime-tools).

Extra R packages can be selected with `packages = ps: [ ps.ggplot2 ];`.
The default fontconfig configuration includes DejaVu and IPAexGothic for
graphics devices that use fontconfig. Set `fonts` to supply another list.
The host clears `TMPDIR` between calls. The SDK restores R's `tempdir()`
before each call. Keep files that need to survive across calls under `HOME`.
Uncaught R errors report the source and line of the innermost Card function
when its source reference is available.

Build `checks.<system>.rAbi9` to run the R Card through the ABI harness.
