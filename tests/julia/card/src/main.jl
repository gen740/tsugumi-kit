import Base64

function main(input)
    item = input.arrival["value"]
    known = item["file"]
    @assert isdir(open(known)) && open(input.direct) == open(known)
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
    @assert params.offset in (Int64(100), Int64(200)) && params.labels isa Tuple
    @assert params.flag && params.ratio === 1.0 && params.labels[1] == "日本語"
    @assert !cancelled() && remaining_ms() == 500
    number = item["value"] isa Integer ? item["value"] : 20
    number < 0 && return (;)

    artifact, directory = dir(Artifact)
    file = joinpath(directory, "value.txt")
    write(file, "test")
    uploaded = upload(Artifact, file)
    choice, _ = dir(Artifact; keep=false)
    view = view_open("listing", VIEW_WINDOW, VIEW_HIDE_CURSOR | VIEW_LOCK_POINTER, 640, 480)
    route = route_register(view.session)
    id(first) = ntuple(i -> i == 1 ? first : 0x00, 16)
    @assert view == (; route=id(0x22), session=id(0x11)) && route == id(0x33)
    @assert recv() == (; kind=:message, route, session=view.session, data=UInt8[42])
    @assert recv(0) == (; kind=:timeout, route=nothing, session=nothing, data=nothing)
    @assert recv(1) == (; kind=:cancelled, route=nothing, session=nothing, data=nothing)
    route_unregister(route)
    send(view.session, UInt8[42])
    send(view.session, fill(UInt8(42), 1 << 24))
    @assert try
        send(view.session, fill(UInt8(42), (1 << 24) + 5))
        false
    catch err
        err isa ErrorException
    end
    @assert recv() == (; kind=:close, route=view.route, session=view.session, data=UInt8[])
    Threads.@spawn begin
        event = recv()
        if event.kind === :message
            background = view_open("background")
            send(background.session, event.data)
        end
    end
    return (; listing=(; nested=(; file=artifact), files=[uploaded, known],
                        choice=(; type="File", value=choice), value=params.offset+number))
end
