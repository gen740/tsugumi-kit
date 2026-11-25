effects <- dyn.load("@EFFECTS@")

main <- function(input) {
  item <- input$arrival$value
  known <- item$file
  stopifnot(dir.exists(artifact_open(known)),
            identical(artifact_open(known), artifact_open(input$direct)))
  envelope <- input$batch$items[[1L]]
  bytes <- jsonlite::base64_dec(envelope$bytes)
  read_i32 <- function(offset) {
    readBin(bytes[offset + seq_len(4L)], integer(), size = 4L, endian = "little")
  }
  table <- read_i32(0L)
  vtable <- table - read_i32(table)
  field <- readBin(bytes[vtable + 4L + seq_len(2L)], integer(), size = 2L,
                   signed = FALSE, endian = "little")
  stopifnot(length(input$batch$items) == 1L,
            envelope$schema == strrep("4", 64), read_i32(table + field) == 4L,
            inherits(params$offset, "tsugumi_int64"),
            unclass(params$offset) %in% c("100", "200"), params$flag, identical(params$ratio, 1.0), params$labels[[1L]] == "日本語",
            !cancelled(), remaining_ms() == 500)
  number <- if (is.numeric(item$value)) item$value else 20
  if (number < 0) return(list())

  nested <- artifact_dir()
  file <- file.path(nested$path, "value.txt")
  writeLines("test", file)
  uploaded <- artifact_upload(file)
  choice <- artifact_dir(keep = FALSE)
  view <- view_open("listing", VIEW_WINDOW, bitwOr(VIEW_HIDE_CURSOR, VIEW_LOCK_POINTER), 640L, 480L)
  route <- route_register(view$session)
  stopifnot(identical(view, list(route = c(as.raw(0x22), raw(15L)),
                                 session = c(as.raw(0x11), raw(15L)))),
            identical(route, c(as.raw(0x33), raw(15L))),
            identical(recv(), list(kind = "message", route = route,
                                    session = view$session, data = as.raw(42))),
            identical(recv(0), list(kind = "timeout", route = NULL,
                                     session = NULL, data = NULL)),
            identical(recv(1), list(kind = "cancelled", route = NULL,
                                     session = NULL, data = NULL)))
  route_unregister(route)
  send(view$session, as.raw(42))
  send(view$session, rep(as.raw(42), 2^24))
  stopifnot(isTRUE(tryCatch({
    send(view$session, rep(as.raw(42), 2^24 + 5))
    FALSE
  }, error = function(error) TRUE)))
  view_close(view$session)
  stopifnot(tryCatch({
    route_register(view$session)
    FALSE
  }, tsugumi_window_closed = function(error) TRUE))
  stopifnot(identical(recv(), list(kind = "close", route = view$route,
                         session = view$session, data = raw(0L))))
  .Call("start_effects", PACKAGE = effects[["name"]])
  list(listing = list(
    nested = list(file = nested$artifact), files = list(uploaded, known),
    choice = list(type = "File", value = choice$artifact),
    value = as.integer(params$offset) + number
  ))
}
