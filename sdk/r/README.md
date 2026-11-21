# R SDK for Card ABI 9

Build the Card with `kit.lib.${system}.rCard { inherit src contract; }`. Pass the
directory containing `main.R` as `src` and the core-generated directory with
`_contract.json`, `_inputs.fbs`, `_outputs.fbs` and `_params.fbs` as `contract`.
The derivation contains `lib/main.so`. Set `entry` to select another R file within
`src`. Extra R packages can be selected with `packages = ps: [ ps.ggplot2 ];`.

Define `main` as a function with a single argument. Its input is a named list
whose keys are the input ports and `params`. If a port is named `params`, find it
under `params_` (with more underscores if needed to avoid other port names).
Return a named list of outputs using
the table fields from the output schemas. Omit optional ports or return `NULL`
for them. A missing required output and a value that fails conversion produce a
Card failure. An uncaught R error produces a Card failure with the source and
line of the innermost Card function when its source reference is available.

```r
main <- function(input) {
  if (cancelled()) stop("Cancelled before reading the input")
  result <- "result.txt"
  writeLines(readLines(input$source$value), result)
  display("Wrote result.txt")
  list(result = list(value = result))
}
```

File and Directory ports use a table with a `value` path. Create new outputs
under the working directory and close them before returning. Return an input
path unchanged to forward it. R starts and sources the Card on the first valid
call in each worker process. The R session's `tempdir()` lives under `TMPDIR`.
The host clears it between calls and the SDK restores the directory before
running the Card again. Keep files needed across calls under `HOME`.

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

Call `display(value, record = FALSE)` for text, a number, a data frame or a
matrix. The constructors `markdown(text)`, `scalar(value, label)`,
`image(bytes, media_type, alt)`, `points(x, y, z, colors)`,
`line_chart(series)`, `plotly(spec)`, `vega_lite(spec)` and
`view_module(module, config, blobs)` select other view modules. Pass
`record = TRUE` to keep an individual display in history.

Use `cancelled()` to poll the host and `remaining_ms()` to read its deadline
budget in milliseconds. If the host's query fails, these return `TRUE` and
`0` respectively. The SDK initializes and evaluates R on one dedicated thread
for the lifetime of the worker process. Call R effect functions from that thread.

Native R extensions can look up these C functions with
`R_GetCCallable("tsugumi", name)` on the interpreter thread and invoke the saved
pointers from their own threads:

| Name | C signature |
| --- | --- |
| `display` | `int (tsugumi_bytes, int record)` |
| `cancelled` | `bool (void)` |
| `remaining_ms` | `double (void)` |

The native display function accepts a serialized ABI 9 `CardDisplay` and returns
zero on success. Keep its bytes alive until it returns. It can display after
`main` returns. Outside an invocation the queries return `true` and `0`.
