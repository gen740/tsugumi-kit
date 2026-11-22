function main(input::Inputs)
    cancelled() && throw(CardFailure(:cancelled, "Stopped before writing"))
    file = "note.txt"
    open(io -> println(io, input.text["value"]), file, "w")
    return (; file)
end
