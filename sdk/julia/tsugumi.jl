module Tsugumi

import JSON
import Printf
import Base64

export Inputs, Artifact, dir, upload, params, send, recv, cancelled, remaining_ms, CardFailure
export Markdown, Scalar, Table, Image, Points, LineChart, Plotly, VegaLite

struct CardFailure <: Exception
    kind::Symbol
    message::String
    cell::String
end
CardFailure(kind::Symbol, message::AbstractString; cell::AbstractString="") =
    CardFailure(kind, String(message), String(cell))
Base.showerror(io::IO, failure::CardFailure) = print(io, failure.message)

include(Main.TSUGUMI_JULIA_CARD_API)

const HOST = Main.TSUGUMI_JULIA_HOST
cancelled() = (@ccall gc_safe=true $(HOST.cancelled)()::Cint) != 0
remaining_ms() = @ccall gc_safe=true $(HOST.remaining_ms)()::Int64

include(Main.TSUGUMI_JULIA_PARAMS)

struct Artifact
    id::NTuple{16,UInt8}
end

function take_path(path)
    path == C_NULL && error("Artifact operation failed")
    try
        return unsafe_string(path)
    finally
        Libc.free(path)
    end
end

function dir(::Type{Artifact})
    id = Vector{UInt8}(undef, 16)
    path = @ccall $(HOST.dir)(id::Ptr{UInt8})::Ptr{UInt8}
    return Artifact(Tuple(id)), take_path(path)
end

function upload(::Type{Artifact}, path::AbstractString)
    id = Vector{UInt8}(undef, 16)
    code = @ccall $(HOST.upload)(path::Cstring, id::Ptr{UInt8})::Cint
    code == 0 || error("Artifact upload failed")
    return Artifact(Tuple(id))
end

function Base.open(artifact::Artifact)
    id = collect(artifact.id)
    return take_path(@ccall $(HOST.open)(id::Ptr{UInt8})::Ptr{UInt8})
end

JSON.lower(artifact::Artifact) = Dict("\$artifact" => Base64.base64encode(collect(artifact.id)))

decode_input(item) = item
decode_input(items::Vector) = map(decode_input, items)
function decode_input(item::Dict)
    if haskey(item, "\$artifact")
        return Artifact(Tuple(Base64.base64decode(item["\$artifact"])))
    end
    return Dict{String,Any}(key => decode_input(value) for (key, value) in item)
end

function recv(view::AbstractString, timeout_ms::Integer=-1)
    bytes = Ref{Ptr{UInt8}}(C_NULL)
    size = Ref{Csize_t}(0)
    code = @ccall gc_safe=true $(HOST.recv)(view::Cstring, timeout_ms::Int64,
                     bytes::Ref{Ptr{UInt8}}, size::Ref{Csize_t})::Cint
    code == 1 && return (:timeout, nothing)
    code == 2 && return (:cancelled, nothing)
    code == 0 || error("display receive was rejected")
    size[] == 0 && return (:message, UInt8[])
    return (:message, unsafe_wrap(Vector{UInt8}, bytes[], size[]; own=true))
end

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
    status = Ref{Cint}()
    encoding = @ccall gc_safe=true $(HOST.emit)(kind::Cstring, data::Cstring, status::Ref{Cint})::Cint
    encoding == 0 || throw(ArgumentError("could not encode the display value"))
    status[] == 0 || error("display was rejected")
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
using ..Tsugumi: Inputs, Artifact, dir, upload, params, send, recv, cancelled, remaining_ms, CardFailure
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
        output = Base.invokelatest(CardCode.main, Inputs(decode_input(values["ports"])))
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
