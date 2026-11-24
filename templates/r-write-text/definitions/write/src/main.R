main <- function(input) {
  if (cancelled()) stop("Stopped before writing")
  file <- artifact_dir()
  writeLines(params$value, file.path(file$path, "note.txt"))
  list(file = file$artifact)
}
