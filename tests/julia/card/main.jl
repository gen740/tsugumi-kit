using Base64: base64decode, base64encode

open(io -> println(io, "loaded"), "initialization.marker", "a")

include("failure.jl")

function main(input)
    mode = input.params["mode"]
    mode == 3 && throw(CardFailure(:cancelled, "explicit failure"; cell="fit"))
    mode == 4 && fail_from_include()
    mode == 5 && error("Card threw")
    mode == 6 && return (;)
    mode == 7 && return (; number=(; value=-1))
    mode == 8 && return CardFailure(:input_missing, "returned failure"; cell="load")
    mode == 9 && throw(CardFailure(:unspecified, "reserved failure kind"; cell="fit"))
    mode == 10 && return (; number=input.number, table=(; rows=[nothing]))

    for name in (:number, :table, :file, :directory, :Params, :params)
        hasproperty(input, name) || error("missing property $name")
    end
    !hasproperty(input, :ports) && !hasproperty(input, :absent) || error("unexpected property")
    input.Params["return"] == 43 && input.Params["__value"] == 7 || error("wrong reserved fields")
    input.number["value"] == typemax(UInt64) || error("lost integer precision")
    rows = input.table["rows"]
    rows[1]["measure"] == "NaN" || error("wrong NaN token")
    rows[2]["measure"] == "Infinity" || error("wrong infinity token")
    rows[3]["measure"] == "-Infinity" || error("wrong negative infinity token")
    rows[1]["maybe"] === nothing || error("missing optional scalar")
    rows[2]["maybe"] == 0 || error("lost zero-valued optional scalar")
    rows[1]["label"] == "Special" || error("wrong enum")
    base64decode(rows[1]["bytes"]) == UInt8[0, 13, 255] || error("wrong byte vector")
    rows[1]["choice"]["type"] == "Note" || error("wrong union")
    rows[1]["choice"]["value"]["value"] == "日本語" || error("wrong union value")
    rows[1]["measure"] = NaN
    rows[2]["measure"] = Inf
    rows[3]["measure"] = -Inf
    rows[1]["bytes"] = base64encode(UInt8[255, 13, 0])

    if mode == 11
        cancelled() && remaining_ms() == 0 || error("wrong fallbacks for throwing host")
        try
            display("throwing host")
            error("expected display rejection")
        catch err
            err isa ErrorException && err.msg == "display was rejected" || rethrow()
        end
    elseif 12 <= mode <= 14
        check_gc_during_effect(mode, input.params["fd"])
    elseif mode == 15 || mode == 16
        effect_task[] = Threads.@spawn :default begin
            mode == 15 ? cancelled() : remaining_ms() == -1
        end
        read_signal(input.params["fd"])
        display("returning")
    elseif mode == 17
        fetch(effect_task[]) || error("wrong value from overlapping host callback")
    end

    if mode == 1
        !cancelled() && remaining_ms() == 500 || error("wrong active effects")
        display("résumé\n\"")
        display(Markdown("**text**"); record=true)
        display(2.5)
        display(Table(["n"], [[1]]))
        display(Image(UInt8[0, 13, 255]; media_type="image/png", alt="sample"))
        display(Points([1], [1], [1]; colors=[0x123456]))
        display(LineChart([(id="id", label="line", x=[2], y=[2])]))
        display(Plotly((; data=[])))
        display(VegaLite((; mark="point")))
        display(View("custom"; blobs=Dict("config"=>Vector{UInt8}("{\"key\":1}"), "sample"=>UInt8[0, 13, 255])); record=true)
        fd = input.params["fd"]
        # The embedding thread owns the interactive pool in this fixture.
        Threads.@spawn :default begin
            read_signal(fd)
            GC.gc()
            cancelled() && remaining_ms() == 0 || error("host still active outside the call")
            display("outside")
        end
    end

    file = forward = nothing
    if input.file !== nothing
        source = path(input, "file")
        open(io -> write(io, uppercase(read(source, String))), "output.txt", "w")
        file = (; value="output.txt")
        forward = (; value=source)
    end
    directory = input.directory === nothing ? nothing : (; value=path(input, "directory"))
    return (; number=input.number, table=(; rows), file, forward, directory, switch=input.Params)
end

const effect_task = Ref{Task}()

function read_signal(fd)
    signal = Ref{UInt8}(0)
    received = @ccall gc_safe=true read(fd::Cint, signal::Ref{UInt8}, 1::Csize_t)::Cssize_t
    received == 1 && signal[] == UInt8('!') || error("read failed")
end

function check_gc_during_effect(mode, fd)
    task = Threads.@spawn :default begin
        read_signal(fd)
        GC.gc()
        signal = Ref(UInt8('!'))
        sent = @ccall gc_safe=true write(fd::Cint, signal::Ref{UInt8}, 1::Csize_t)::Cssize_t
        sent == 1 || error("write failed")
    end
    if mode == 12
        display("waiting for GC")
    elseif mode == 13
        !cancelled() || error("wrong cancellation status")
    else
        remaining_ms() == -1 || error("lost no-deadline sentinel")
    end
    fetch(task)
end
