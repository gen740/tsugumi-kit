# Sourced once into the SDK environment, which is the Card environment's parent.
.tsugumi_json <- function(value) {
  # Use enough precision for a double round trip and retain special floats.
  jsonlite::toJSON(value, auto_unbox = TRUE, digits = 17, null = "null",
                   na = "string", raw = "base64", dataframe = "rows")
}

.tsugumi_display <- function(kind, value, record) {
  if (!is.logical(record) || length(record) != 1L || is.na(record)) {
    stop("record must be TRUE or FALSE")
  }
  .Call("tsugumi_r_display", .tsugumi_json(list(kind = kind, value = value)),
        record)
  invisible(NULL)
}

display <- function(value, record = FALSE) UseMethod("display")
display.character <- function(value, record = FALSE) {
  .tsugumi_display("text", paste(value, collapse = "\n"), record)
}
display.numeric <- function(value, record = FALSE) {
  display(scalar(value), record)
}
display.integer <- display.numeric
display.double <- display.numeric
display.data.frame <- function(value, record = FALSE) {
  columns <- lapply(value, format, trim = TRUE)
  rows <- lapply(seq_len(nrow(value)), function(index) {
    I(vapply(columns, `[[`, character(1), index, USE.NAMES = FALSE))
  })
  .tsugumi_display("table", list(columns = I(names(value)), rows = rows), record)
}
display.matrix <- function(value, record = FALSE) {
  display(as.data.frame(value), record)
}
display.tsugumi_view <- function(value, record = FALSE) {
  .tsugumi_display(value$kind, value$value, record)
}

.tsugumi_view <- function(kind, value) {
  structure(list(kind = kind, value = value), class = "tsugumi_view")
}

scalar <- function(value, label = "") {
  if (!is.numeric(value) || length(value) != 1L) stop("expected a scalar")
  .tsugumi_view("scalar", list(value = value, label = label))
}
markdown <- function(text) {
  .tsugumi_view("markdown", paste(text, collapse = "\n"))
}
image <- function(bytes, media_type = "image/png", alt = "") {
  .tsugumi_view("image", list(bytes = jsonlite::base64_enc(bytes),
                             media_type = media_type, alt = alt))
}
points <- function(x, y, z, colors = numeric()) {
  .tsugumi_view("points", list(x = I(x), y = I(y), z = I(z), colors = I(colors)))
}
line_chart <- function(series) {
  .tsugumi_view("line-chart", unname(lapply(series, function(item) {
    list(id = item$id, label = item$label, x = I(item$x), y = I(item$y))
  })))
}
plotly <- function(spec) .tsugumi_view("plotly", spec)
vega_lite <- function(spec) .tsugumi_view("vega-lite", spec)
view_module <- function(module, config = NULL, blobs = list()) {
  if (length(blobs) && (is.null(names(blobs)) || any(!nzchar(names(blobs))))) {
    stop("blobs must be named")
  }
  encoded <- lapply(blobs, jsonlite::base64_enc)
  if (!length(encoded)) encoded <- setNames(list(), character())
  .tsugumi_view("module", list(module = module, config = config, blobs = encoded))
}

cancelled <- function() .Call("tsugumi_r_cancelled")
remaining_ms <- function() .Call("tsugumi_r_remaining_ms")

.tsugumi_state <- new.env(parent = emptyenv())

.tsugumi_decode_input <- function(value) {
  if (!is.list(value)) return(value)
  numbers <- value[["$numeric"]]
  if (is.null(numbers)) return(lapply(value, .tsugumi_decode_input))
  # Wide integers are decimal strings, since a double may round them.
  wide_integer <- function(item) {
    is.character(item) && !(item %in% c("NaN", "Inf", "-Inf"))
  }
  if (any(vapply(numbers, wide_integer, logical(1L)))) return(numbers)
  vapply(numbers, as.double, numeric(1L))
}

.tsugumi_failure <- function(error, root) {
  failure <- list(kind = "worker_failed", message = conditionMessage(error))
  # Use the innermost Card function with a retained R source reference.
  functions <- lapply(rev(seq_len(sys.nframe())), sys.function)
  for (fun in functions) {
    ref <- attr(fun, "srcref")
    file <- attr(ref, "srcfile")
    if (!is.null(file) && startsWith(file$filename, paste0(root, "/"))) {
      failure$source <- substring(file$filename, nchar(root) + 2L)
      failure$line <- as.integer(ref[[1L]])
      break
    }
  }
  failure
}

.tsugumi_invoke <- function(json, path, root) {
  failure <- NULL
  on_error <- function(error) {
    failure <<- .tsugumi_failure(error, root)
  }
  reply <- tryCatch(withCallingHandlers({
    # The host clears TMPDIR between calls, including R's session directory.
    # Restore the directory at the path R and its packages already use.
    scratch <- tempdir()
    if (!dir.exists(scratch) &&
        !dir.create(scratch, mode = "0700", showWarnings = FALSE)) {
      stop("Cannot restore R temporary directory")
    }
    if (is.null(.tsugumi_state$entry)) {
      if (!is.null(.tsugumi_state$load_failure)) {
        return(.tsugumi_json(list(failure = .tsugumi_state$load_failure)))
      }
      card <- new.env(parent = environment(.tsugumi_invoke))
      .tsugumi_state$entry <- tryCatch(withCallingHandlers({
        sys.source(path, envir = card, keep.source = TRUE)
        entry <- get("main", envir = card, inherits = FALSE)
        if (!is.function(entry) || length(formals(entry)) != 1L ||
            identical(names(formals(entry)), "...")) {
          stop("main must be a function with one input argument")
        }
        entry
      }, error = on_error), error = function(error) {
        .tsugumi_state$load_failure <- failure
        NULL
      })
      if (is.null(.tsugumi_state$entry)) {
        return(.tsugumi_json(list(failure = .tsugumi_state$load_failure)))
      }
    }
    input <- lapply(jsonlite::fromJSON(json, simplifyVector = FALSE), .tsugumi_decode_input)
    output <- .tsugumi_state$entry(input)
    output_names <- names(output)
    if (is.null(output) || (is.list(output) && !length(output))) {
      output <- setNames(list(), character())
    } else if (!is.list(output) || is.null(output_names) ||
               anyNA(output_names) || any(!nzchar(output_names)) ||
               anyDuplicated(output_names)) {
      return(.tsugumi_json(list(failure = list(
        kind = "output_invalid", message = "main must return a named list of outputs"
      ))))
    }
    tryCatch(.tsugumi_json(list(output = output)), error = function(error) {
      .tsugumi_json(list(failure = list(
        kind = "output_invalid", message = conditionMessage(error)
      )))
    })
  }, error = on_error), error = function(error) {
    .tsugumi_json(list(failure = failure))
  })
  reply
}
