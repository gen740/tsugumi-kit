main <- function(input) {
  if (cancelled()) stop("Stopped before writing")
  file <- artifact_dir(keep = params$keep_output)
  writeLines(params$value, file.path(file$path, "note.txt"))
  list(file = file$artifact)
}
