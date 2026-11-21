# R SDK for Card ABI 9

Build the Card with `kit.lib.${system}.rCard { inherit src contract; }`. Pass the
directory containing `main.R` as `src` and the core-generated directory with
`_contract.json`, `_inputs.fbs`, `_outputs.fbs` and `_params.fbs` as `contract`.
The derivation contains `lib/main.so`. Set `entry` to select another R file within
`src`. Extra R packages can be selected with `packages = ps: [ ps.ggplot2 ];`.

Define `main` as a function with a single argument. Its input is a named list
whose keys are the input ports and `params`. Return a named list of outputs using
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
call in each worker process. The R session's `tempdir()` lives under `HOME` and
survives the worker's per-call cleanup of `TMPDIR`.

## R values

- FlatBuffers tables and structs use named lists. Vectors and fixed arrays
  arrive as R lists. Use an unnamed list or `I(vector)` to return a JSON array
  even when it has one element. A missing nullable scalar or child table
  arrives as `NULL`. The generated object API represents absent strings and
  vectors by empty values.
- Known enum values use their schema names. A union uses
  `list(type = "Variant", value = ...)` and an unset union uses `NULL`.
- Integers of magnitude greater than `2^53` travel as decimal strings. Use
  decimal strings to produce these values from R. The floating-point tokens
  are `"NaN"`, `"Inf"` and `"-Inf"`. R's corresponding floating-point values
  can also be returned directly.
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
budget in milliseconds. The embedded R interpreter runs on the worker thread
that owns the Card. Ordinary R calls to the SDK functions run on that thread.
