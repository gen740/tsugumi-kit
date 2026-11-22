# Filter channel counts

One C++ Card keeps detector channels whose count meets a threshold. **Targets unreleased Card ABI 9.** This template will not build in released Tsugumi until the ABI 9 PR lands. The builder injection in `default.nix` is provisional.

Connect an upstream **File** output to `filter.counts`. The example accepts a narrow CSV format: the header `channel,counts`, then two unquoted unsigned 64-bit decimal integers per line, with LF or CRLF endings. A malformed row fails the call and reports its line number.

```csv
channel,counts
1,8
2,10
3,17
```

With the default `minimum` of 10, `filter.filtered` is a new File containing the header and rows `2,10` and `3,17`. The Card records a table preview of the first ten matching rows and forwards the original File unchanged on `filter.original`. If no rows match, the new file contains just the header.

The Card's `default.nix` exposes the C++ SDK's bundled `views`, including the `table` view used for the preview.

Import this folder using the Git subdirectory `templates/filter-counts`. [Template guide](../../docs/templates.md) · [C++ source](filter/src/card.cc)
