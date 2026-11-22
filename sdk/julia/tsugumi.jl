module Tsugumi

import JSON
import Printf

export Inputs, path, send, cancelled, remaining_ms, CardFailure
export Markdown, Scalar, Table, Image, Points, LineChart, Plotly, VegaLite

struct Inputs
    ports::Dict{String,Any}
    params::Dict{String,Any}
end

Base.getindex(input::Inputs, name::AbstractString) = getfield(input, :ports)[name]
Base.getproperty(input::Inputs, name::Symbol) =
    name === :params ? getfield(input, :params) : input[String(name)]
Base.propertynames(input::Inputs, private::Bool=false) =
    (:params, (Symbol(name) for name in keys(getfield(input, :ports)) if name != "params")...)
path(input::Inputs, name::AbstractString) = input[name]["value"]

struct CardFailure <: Exception
    kind::Symbol
    message::String
    cell::String
end
CardFailure(kind::Symbol, message::AbstractString; cell::AbstractString="") =
    CardFailure(kind, String(message), String(cell))
Base.showerror(io::IO, failure::CardFailure) = print(io, failure.message)

const HOST = Main.TSUGUMI_JULIA_HOST
cancelled() = (@ccall gc_safe=true $(HOST.cancelled)()::Cint) != 0
remaining_ms() = @ccall gc_safe=true $(HOST.remaining_ms)()::Int64

struct Markdown
    text::String
end
struct Scalar
    value::Float64
    label::String
end
Scalar(value::Real; label="") = Scalar(value, label)

struct Table
    columns::Vector{String}
    rows::Vector{Vector{String}}
    Table(columns, rows) = new(collect(string.(columns)), [collect(string.(row)) for row in rows])
end

struct Image
    bytes::Vector{UInt8}
    media_type::String
    alt::String
end
Image(bytes::AbstractVector{UInt8}; media_type, alt="") = Image(bytes, media_type, alt)

struct Points
    x::Vector{Float32}
    y::Vector{Float32}
    z::Vector{Float32}
    colors::Vector{UInt32}
end
Points(x, y, z; colors=UInt32[]) = Points(x, y, z, colors)

struct LineChart
    series::Vector{NamedTuple}
end
struct Plotly
    config::Any
end
struct VegaLite
    config::Any
end
function send(view::AbstractString, bytes::AbstractVector{UInt8})
    data = convert(Vector{UInt8}, bytes)
    code = @ccall gc_safe=true $(HOST.send)(view::Cstring, data::Ptr{UInt8}, length(data)::Csize_t)::Cint
    code == 0 || error("display was rejected")
    return nothing
end

function emit(kind, value)
    data = JSON.json(value)
    code = @ccall gc_safe=true $(HOST.emit)(kind::Cstring, data::Cstring)::Cint
    code == 0 || error("display was rejected")
    return nothing
end

send(value::AbstractString) = emit("text", value)
send(value::Markdown) = emit("markdown", value.text)
send(value::Real) = send(Scalar(value))
function send(value::Scalar)
    number = isfinite(value.value) ? Printf.@sprintf("%.6g", value.value) : "\u2014"
    send(Markdown(isempty(value.label) ? number : "**$(value.label)**: $number"))
end
send(value::Table) = emit("table", value)
send(value::Image) = emit("image", value)
send(value::Points) = emit("points", value)
send(value::LineChart) = emit("line-chart", value.series)
send(value::Plotly) = emit("plotly", value.config)
send(value::VegaLite) = emit("vega-lite", value.config)

# JSON.jl normally writes nonfinite values as null. Keep the three ABI-internal
# tokens distinct from null and let the generated glue interpret float fields.
struct PortSerialization <: JSON.Serializations.CommonSerialization end
function JSON.show_json(io::JSON.Writer.StructuralContext, s::PortSerialization, value::AbstractFloat)
    if isfinite(value)
        invoke(JSON.show_json,
               Tuple{JSON.Writer.StructuralContext,JSON.Serializations.CommonSerialization,AbstractFloat},
               io, s, Float64(value))
    else
        JSON.show_json(io, s, isnan(value) ? "NaN" : signbit(value) ? "-Infinity" : "Infinity")
    end
end

module CardCode
using ..Tsugumi: Inputs, path, send, cancelled, remaining_ms, CardFailure
using ..Tsugumi: Markdown, Scalar, Table, Image, Points, LineChart, Plotly, VegaLite
end

const source_root = Ref("")
const source_prefix = Ref("")
const loaded = Ref(false)
const load_failure = Ref("")

function failure_json(err, bt=nothing)
    source = ""
    line = 0
    for frame in (bt === nothing ? () : stacktrace(bt))
        file = String(frame.file)
        if frame.line > 0 && startswith(file, source_root[] * "/")
            source = relpath(file, source_root[])
            line = frame.line
            break
        end
    end
    detail = err
    while detail isa LoadError
        file = detail.file
        if isempty(source) && startswith(file, source_root[] * "/")
            source = relpath(file, source_root[])
            line = detail.line
        end
        detail = detail.error
    end
    kind = detail isa CardFailure ? String(detail.kind) : "worker_failed"
    cell = detail isa CardFailure ? detail.cell : ""
    message = detail isa CardFailure ? detail.message : sprint(showerror, err)
    isempty(source) || (source = joinpath(source_prefix[], source))
    return JSON.json((; failure=(; kind, message, source, line, cell)))
end

function load(root, entry, prefix)
    source_root[] = root
    source_prefix[] = prefix
    try
        Base.include(CardCode, joinpath(root, entry))
        isdefined(CardCode, :main) || error("define main(input) in " * entry)
        loaded[] = true
    catch err
        load_failure[] = failure_json(err, catch_backtrace())
    end
    return nothing
end

function run(payload)
    loaded[] || return load_failure[]
    try
        values = JSON.parse(payload; inttype=Int128, allownan=false)
        output = Base.invokelatest(CardCode.main, Inputs(values["ports"], values["params"]))
        if output isa CardFailure
            return failure_json(output)
        end
        if !(output isa Union{AbstractDict,NamedTuple})
            return JSON.json((; failure=(; kind="output_invalid", message="return a Dict or NamedTuple of output ports")))
        end
        io = IOBuffer()
        JSON.show_json(io, PortSerialization(), (; output))
        return String(take!(io))
    catch err
        return failure_json(err, catch_backtrace())
    end
end

end
