# Julia Cards

Import `<tsugumi-kit/sdk/julia>` with the Card's `pkgs`.
`juliaCard` builds the worker library and `juliaApi` publishes the
generated API under `share/tsugumi/api/`.

Open a declared view and send raw bytes to its session.

```julia
opened = view_open("preview", VIEW_WINDOW, 640, 480)
send(opened.session, UInt8[42])
event = recv(500)
```

Sessions and routes are `NTuple{16,UInt8}` values. Register a route with
`route_register(session)` to receive events for a session from another Card.
`route_unregister(route)` drops the route. Each event has `kind`, `route`,
`session` and `data` fields. `kind` is `:message`, `:close`, `:timeout`
or `:cancelled`. Close has an empty byte vector. Timeout and cancellation
have `nothing` for the other three fields.

- [Julia SDK guide](../../docs/julia-sdk.md)
- [Julia Card starter](../../templates/julia-write-text/)
- [Card SDK contract](../../docs/sdk-contract.md)
