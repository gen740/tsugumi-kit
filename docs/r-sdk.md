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
  writeLines(params$value, file.path(file$path, "note.txt"))
  list(file = file$artifact)
}
```

The [R starter](../templates/r-write-text/) uses this entry with a FlatBuffer
text parameter and an Artifact `file` output.

## Inputs and outputs

Read a connected input with `input$text` or `input[["text"]]`. Use
`optional_input(input, "text")` to handle a disconnected port. The
build-time `params` list contains logical, double, character and list
values. Integer params are exact decimal strings with class
`tsugumi_int64`, which preserves the full signed 64-bit range.

A mixed Merge yields `list(type = "Member", value = table)` after the
SDK matches and verifies the candidate by schema ID. Collect yields
opaque envelopes with `schema` and `bytes`. Return a
named list of outputs. Omit optional outputs or set them to `NULL`.
A missing required output produces `output_invalid`.

`artifact_dir()` returns an Artifact and a writable directory in
`$artifact` and `$path`. `artifact_upload(path)` copies a file or
directory. `artifact_open(artifact)` returns the read-only directory of
any sealed Artifact of the project whose ID the Card knows, such as one in
an input. Forward an Artifact by returning it in a
declared Artifact field.

Both keep the Artifact after its run by default. With `keep = FALSE` it
exists only while its run runs: later Cards of the same run can open it,
and it is removed with its record when the run ends. Opening it after that
signals an error saying the Artifact is not available. Read keep from a
parameter to let the person running choose:

```nix
# params.nix
{
  keep_scratch = false;
}
```

```r
scratch <- artifact_dir(keep = params$keep_scratch)
```

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

`view_open(name, mode, flags, width, height)` returns a list with `route` and `session`.
Use `route_register(session)` and `route_unregister(route)` to manage routes.
`view_close(session)` closes the window. If the window has closed,
`route_register` signals a `tsugumi_window_closed` condition.
`send(session, bytes)` sends one raw vector. `send(value)` opens a panel
and encodes text, a number, a data frame or a matrix.
The constructors `markdown(text)`, `scalar(value, label)`,
`image(bytes, media_type, alt)`, `points(x, y, z, colors)`,
`line_chart(series)`, `plotly(spec)` and `vega_lite(spec)` select other
views. Local argument and encoding errors raise an R error before calling
the host. If the host cannot accept the message, `send` raises
`"display rejected"`.

Combine `VIEW_HIDE_CURSOR` and `VIEW_LOCK_POINTER` with `bitwOr()`.
The default flags value is `0L`.

`recv(timeout_ms)` returns a list with `kind`, `route`, `session` and
`data`. Kind is `"message"`, `"close"`, `"timeout"` or `"cancelled"`.
A negative timeout waits without a deadline. The SDK frames typed values.

The SDK bundles the `line-chart` and `points` HTML views.
Declare the matching views in your Card's `default.nix` with
`views = sdk.views ++ [ ... ];`.
The bundled `text` view displays values sent with `send("text")`.
See the [view protocol](sdk-contract.md#display-and-effects).

Use `cancelled()` to poll the host and `remaining_ms()` to read its deadline
budget in milliseconds. If the host's query fails, these return `TRUE` and
`0` respectively. The SDK initializes and evaluates R on one dedicated thread
for the lifetime of the worker process. Call R effect functions from that thread.

Native R extensions can look up these C functions with
`R_GetCCallable("tsugumi", name)` on the interpreter thread and invoke the saved
pointers from their own threads. Include `<tsugumi/abi/card_abi.h>` for the
view types.

| Name | C signature |
| --- | --- |
| `view_open` | `int (const char* view, uint32_t mode, uint32_t flags, uint32_t width, uint32_t height, tsugumi_route* route, tsugumi_view_session* session)` |
| `view_close` | `int (const tsugumi_view_session* session)` |
| `route_register` | `int (const tsugumi_view_session* session, tsugumi_route* route)` |
| `route_unregister` | `int (const tsugumi_route* route)` |
| `send` | `int (const tsugumi_view_session* session, const uint8_t* bytes, size_t size)` |
| `recv` | `int (int64_t timeout_ms, tsugumi_view_event* event)` |
| `cancelled` | `bool (void)` |
| `remaining_ms` | `double (void)` |

The native send function returns zero on success. Keep the session and bytes
alive until it returns. Native threads can send and receive after `main`
returns. Receive returns 0 for an event, 1 for timeout, 2 for cancellation,
and a negative value for an error. Initialize the event to zero and release
the returned bytes with `free((void*)event.data.data)`. Outside an invocation
the queries return `true` and `0`.

The builder accepts `tools` for runtime executables. See [Runtime tools](../README.md#runtime-tools).

Extra R packages can be selected with `packages = ps: [ ps.ggplot2 ];`.
The default fontconfig configuration includes DejaVu and IPAexGothic for
graphics devices that use fontconfig. Set `fonts` to supply another list.
The host clears `TMPDIR` between calls. The SDK restores R's `tempdir()`
before each call. Keep files that need to survive across calls under `HOME`.
Uncaught R errors report the source and line of the innermost Card function
when its source reference is available.

Build `checks.<system>.rAbi` to run the R Card through the ABI harness.
