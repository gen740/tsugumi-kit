# R Cards

Import `<tsugumi-kit/sdk/r>` with the Card's `pkgs`.
`rCard` builds the worker library and `rApi` publishes the
generated API under `share/tsugumi/api/`.

Open a declared view and send raw bytes to its session.

```r
opened <- view_open("preview", VIEW_WINDOW, bitwOr(VIEW_HIDE_CURSOR, VIEW_LOCK_POINTER), 640L, 480L)
send(opened$session, charToRaw("ready"))
event <- recv(timeout_ms = 500L)
```

Sessions and routes are 16-byte raw vectors. Register a route with
`route_register(session)` to receive events for a session from another Card.
`route_unregister(route)` drops the route. Each event has `kind`, `route`,
`session` and `data` fields. `kind` is `"message"`, `"close"`, `"timeout"`
or `"cancelled"`. Close has empty raw data. Timeout and cancellation have
`NULL` for the other three fields.

- [R SDK guide](../../docs/r-sdk.md)
- [R Card starter](../../templates/r-write-text/)
- [Card SDK contract](../../docs/sdk-contract.md)
