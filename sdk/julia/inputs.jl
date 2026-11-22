# Only the inputs that arrived are present. Indexing reads an input the Card
# needs; `get` with a default reads one it can do without.
struct Inputs
    ports::Dict{String,Any}
    params::Dict{String,Any}
end

const PortName = Union{AbstractString,Symbol}
Base.haskey(input::Inputs, name::PortName) = haskey(getfield(input, :ports), String(name))
Base.get(input::Inputs, name::PortName, default) = get(getfield(input, :ports), String(name), default)
function Base.getindex(input::Inputs, name::PortName)
    ports = getfield(input, :ports)
    haskey(ports, String(name)) || throw(CardFailure(:input_missing, "Missing input $name"))
    return ports[String(name)]
end
Base.getproperty(input::Inputs, name::Symbol) =
    name === :params ? getfield(input, :params) : input[name]
Base.hasproperty(input::Inputs, name::Symbol) = name === :params || haskey(input, name)
Base.propertynames(input::Inputs, private::Bool=false) =
    (:params, (Symbol(name) for name in keys(getfield(input, :ports)) if name != "params")...)
