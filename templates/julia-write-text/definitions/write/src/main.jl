function main(input::Inputs)
    cancelled() && throw(CardFailure(:cancelled, "Stopped before writing"))
    file, directory = dir(Artifact)
    open(io -> println(io, params.value), joinpath(directory, "note.txt"), "w")
    return (; file)
end
