import Base64

function main(input)
    item = input.arrival["value"]
    granted = item["file"]
    @assert isdir(open(granted)) && open(input.direct) == open(granted)
    envelope = only(input.batch["items"])
    bytes = Base64.base64decode(envelope["bytes"])
    io = IOBuffer(bytes)
    table = ltoh(read(io, Int32))
    seek(io, table)
    vtable = table - ltoh(read(io, Int32))
    seek(io, vtable + 4)
    field = ltoh(read(io, UInt16))
    seek(io, table + field)
    @assert envelope["schema"] == repeat("4", 64) && ltoh(read(io, Int32)) == 4
    @assert params.offset === Int64(100) && params.labels isa Tuple
    @assert params.flag && params.ratio === 1.0 && params.labels[1] == "日本語"
    @assert !cancelled() && remaining_ms() == 500
    number = item["value"] isa Integer ? item["value"] : 20
    number < 0 && return (;)

    artifact, directory = dir(Artifact)
    file = joinpath(directory, "value.txt")
    write(file, "test")
    uploaded = upload(Artifact, file)
    choice, _ = dir(Artifact)
    @assert recv("request") == (:message, UInt8[42])
    @assert recv("request", 0) == (:timeout, nothing)
    @assert recv("request", 1) == (:cancelled, nothing)
    send("listing", UInt8[42])
    send("listing", fill(UInt8(42), (1 << 24) + 5))
    Threads.@spawn begin
        status, data = recv("background")
        status === :message && send("background", data)
    end
    return (; listing=(; nested=(; file=artifact), files=[uploaded, granted],
                        choice=(; type="File", value=choice), value=params.offset+number))
end
