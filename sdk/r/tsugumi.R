# Sourced once into the SDK environment, which is the Card environment's parent.
.tsugumi_json <- function(value) {
  # Use enough precision for a double round trip and retain special floats.
  jsonlite::toJSON(.tsugumi_encode_output(value), auto_unbox = TRUE, digits = 17, null = "null",
                   na = "string", raw = "base64", dataframe = "rows")
}

.tsugumi_encode_output <- function(value) {
  if (inherits(value, "tsugumi_artifact") || inherits(value, "tsugumi_int64")) {
    return(unclass(value))
  }
  if (!is.list(value) || is.data.frame(value)) return(value)
  result <- lapply(value, .tsugumi_encode_output)
  attributes(result) <- attributes(value)
  result
}

VIEW_PANEL <- 0L
VIEW_WINDOW <- 1L
VIEW_FULLSCREEN <- 2L

view_open <- function(view, mode = VIEW_PANEL, width = 0L, height = 0L) {
  result <- .Call(tsugumi_r_view_open, view, mode, width, height)
  setNames(result, c("route", "session"))
}

route_register <- function(session) {
  .Call(tsugumi_r_route_register, session)
}

route_unregister <- function(route) {
  .Call(tsugumi_r_route_unregister, route)
  invisible(NULL)
}

send <- function(value, bytes) {
  if (!missing(bytes)) {
    .Call(tsugumi_r_send, value, bytes)
    return(invisible(NULL))
  }
  UseMethod("send")
}

.tsugumi_display <- function(kind, value) {
  .Call(tsugumi_r_emit, .tsugumi_json(list(kind = kind, value = value)))
  invisible(NULL)
}

send.character <- function(value) {
  .tsugumi_display("text", paste(value, collapse = "\n"))
}
send.numeric <- function(value) {
  send(scalar(value))
}
send.integer <- send.numeric
send.double <- send.numeric
send.data.frame <- function(value) {
  columns <- lapply(value, format, trim = TRUE)
  rows <- lapply(seq_len(nrow(value)), function(index) {
    I(vapply(columns, `[[`, character(1), index, USE.NAMES = FALSE))
  })
  .tsugumi_display("table", list(columns = I(names(value)), rows = rows))
}
send.matrix <- function(value) {
  send(as.data.frame(value))
}
send.tsugumi_view <- function(value) {
  .tsugumi_display(value$kind, value$value)
}

.tsugumi_view <- function(kind, value) {
  structure(list(kind = kind, value = value), class = "tsugumi_view")
}

scalar <- function(value, label = "") {
  if (!is.numeric(value) || length(value) != 1L) stop("expected a scalar")
  number <- if (is.finite(value)) sprintf("%.6g", value) else "\u2014"
  markdown(if (nzchar(label)) paste0("**", label, "**: ", number) else number)
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
cancelled <- function() .Call(tsugumi_r_cancelled)
remaining_ms <- function() .Call(tsugumi_r_remaining_ms)

recv <- function(timeout_ms = -1) {
  result <- .Call(tsugumi_r_recv, timeout_ms)
  setNames(result, c("kind", "route", "session", "data"))
}

.artifact <- function(id) {
  structure(list(`$artifact` = jsonlite::base64_enc(id)), class = "tsugumi_artifact")
}

# keep = FALSE makes an Artifact that exists only while its run runs.
artifact_dir <- function(keep = TRUE) {
  stopifnot(is.logical(keep), length(keep) == 1L, !is.na(keep))
  created <- .Call(tsugumi_r_artifact_dir, keep)
  list(artifact = .artifact(created[[1L]]), path = created[[2L]])
}

artifact_upload <- function(path, keep = TRUE) {
  stopifnot(is.character(path), length(path) == 1L, !is.na(path))
  stopifnot(is.logical(keep), length(keep) == 1L, !is.na(keep))
  .artifact(.Call(tsugumi_r_artifact_upload, path, keep))
}

artifact_open <- function(artifact) {
  stopifnot(inherits(artifact, "tsugumi_artifact"))
  .Call(tsugumi_r_artifact_open, jsonlite::base64_dec(artifact[["$artifact"]]))
}

.tsugumi_state <- new.env(parent = emptyenv())

.tsugumi_decode_input <- function(value) {
  if (!is.list(value)) return(value)
  id <- value[["$artifact"]]
  if (!is.null(id)) return(.artifact(jsonlite::base64_dec(id)))
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
  kind <- if (inherits(error, "tsugumi_failure")) error$kind else "worker_failed"
  failure <- list(kind = kind, message = conditionMessage(error))
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
    values <- jsonlite::fromJSON(json, simplifyVector = FALSE)
    input <- structure(lapply(values$ports, .tsugumi_decode_input),
                       class = "tsugumi_input")
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
