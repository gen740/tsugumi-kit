use std::collections::HashMap;
use std::ffi::{CStr, c_char, c_int, c_void};
use std::os::unix::ffi::OsStrExt;
use std::panic::{AssertUnwindSafe, Location, catch_unwind};
use std::path::{Component, Path, PathBuf};
use std::sync::{Mutex, MutexGuard};

use flatbuffers::{
    FlatBufferBuilder, Follow, ForwardsUOffset, Verifiable, Verifier, VerifierOptions,
};

use crate::wire::tsugumi::card::{
    CardFailure, CardFailureArgs, CardInput, CardPort, CardPortArgs, CardResult, CardResultArgs,
};
pub use crate::wire::tsugumi::data::FailureKind;
use crate::wire::tsugumi::{Artifact, ArtifactId, ArtifactT};

pub type Result<T> = std::result::Result<T, Failure>;

fn valid_root_offset(bytes: &[u8]) -> bool {
    // Require room for a root offset, a table offset, and a vtable header.
    // The Rust verifier alone can accept a zero root offset for empty tables.
    if !(12..i32::MAX as usize).contains(&bytes.len()) {
        return false;
    }
    let root = u32::from_le_bytes(bytes[..4].try_into().unwrap()) as usize;
    (4..=bytes.len() - 4).contains(&root)
}

fn read_root<'a, T>(bytes: &'a [u8], error: impl FnOnce() -> Failure) -> Result<T>
where
    T: Follow<'a, Inner = T> + Verifiable + 'a,
{
    if !valid_root_offset(bytes) {
        return Err(error());
    }
    flatbuffers::root::<T>(bytes).map_err(|_| error())
}

#[derive(Debug)]
pub struct Failure {
    pub kind: FailureKind,
    pub message: String,
    pub source: Option<String>,
    pub line: u32,
    pub cell: Option<String>,
}

impl Failure {
    #[track_caller]
    pub fn new(kind: FailureKind, message: impl Into<String>) -> Self {
        let mut failure = Self::without_location(kind, message);
        let caller = Location::caller();
        let prefix = option_env!("TSUGUMI_RUST_SOURCE_PREFIX").unwrap_or_default();
        let path = Path::new(prefix).join(caller.file());
        if path
            .components()
            .all(|part| matches!(part, Component::CurDir | Component::Normal(_)))
        {
            let source = path
                .components()
                .filter_map(|part| match part {
                    Component::Normal(name) => name.to_str(),
                    _ => None,
                })
                .collect::<Vec<_>>()
                .join("/");
            if !source.is_empty() {
                failure.source = Some(source);
                failure.line = caller.line();
            }
        }
        failure
    }

    pub fn with_cell(mut self, cell: impl Into<String>) -> Self {
        self.cell = Some(cell.into());
        self
    }

    fn without_location(kind: FailureKind, message: impl Into<String>) -> Self {
        Self {
            kind,
            message: message.into(),
            source: None,
            line: 0,
            cell: None,
        }
    }
}

impl std::fmt::Display for Failure {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.message)
    }
}

impl std::error::Error for Failure {}

impl From<std::io::Error> for Failure {
    #[track_caller]
    fn from(error: std::io::Error) -> Self {
        Self::new(FailureKind::worker_failed, error.to_string())
    }
}

pub(crate) struct Input<'a> {
    ports: HashMap<&'a str, CardPort<'a>>,
}

impl<'a> Input<'a> {
    pub(crate) fn parse(bytes: &'a [u8]) -> Result<Self> {
        let input = read_root::<CardInput>(bytes, || {
            Failure::without_location(FailureKind::input_invalid, "Invalid CardInput")
        })?;
        let mut ports = HashMap::new();
        for port in input.inputs().into_iter().flatten() {
            if ports.insert(port.name(), port).is_some() {
                return Err(Failure::without_location(
                    FailureKind::input_invalid,
                    format!("Duplicate input {}", port.name()),
                ));
            }
        }
        Ok(Self { ports })
    }

    pub(crate) fn take(&mut self, name: &str) -> Result<(&'a str, &'a [u8])> {
        let port = require_input(name, self.ports.remove(name))?;
        match (port.schema(), port.bytes()) {
            (Some(schema), Some(bytes)) => Ok((schema, bytes.bytes())),
            _ => Err(Failure::without_location(
                FailureKind::input_invalid,
                format!("Missing schema or bytes for {name}"),
            )),
        }
    }

    pub(crate) fn read<T>(&mut self, name: &str, expected_schema: &str) -> Result<T>
    where
        T: Follow<'a, Inner = T> + Verifiable + 'a,
    {
        let (schema, bytes) = self.take(name)?;
        if schema != expected_schema {
            return Err(unknown_schema(name));
        }
        read_value(bytes, name)
    }

    pub(crate) fn check_consumed(self) -> Result<()> {
        if self.ports.is_empty() {
            Ok(())
        } else {
            Err(Failure::without_location(
                FailureKind::input_invalid,
                "Unknown input port",
            ))
        }
    }
}

/// Turn an optional input into a needed value, naming the port if it is absent.
pub fn require_input<T>(name: &str, value: Option<T>) -> Result<T> {
    value.ok_or_else(|| {
        Failure::without_location(FailureKind::input_missing, format!("Missing input {name}"))
    })
}

pub(crate) fn read_value<'a, T>(bytes: &'a [u8], name: &str) -> Result<T>
where
    T: Follow<'a, Inner = T> + Verifiable + 'a,
{
    read_root::<T>(bytes, || {
        Failure::without_location(
            FailureKind::input_invalid,
            format!("Invalid FlatBuffer for {name}"),
        )
    })
}

pub(crate) fn unknown_schema(name: &str) -> Failure {
    Failure::without_location(
        FailureKind::input_invalid,
        format!("Unknown schema for {name}"),
    )
}

pub(crate) type OutputPorts<'a> = Vec<flatbuffers::WIPOffset<CardPort<'a>>>;

pub(crate) fn invalid_output(name: &str) -> Failure {
    Failure::without_location(
        FailureKind::output_invalid,
        format!("Invalid output {name}"),
    )
}

pub(crate) fn pack_value<T: Verifiable>(
    name: &str,
    pack: impl FnOnce(&mut FlatBufferBuilder<'_>),
) -> Result<Vec<u8>> {
    let mut value = FlatBufferBuilder::new();
    // flatc's object API can panic when a required field was left empty.
    if catch_unwind(AssertUnwindSafe(|| pack(&mut value))).is_err()
        || ForwardsUOffset::<T>::run_verifier(
            &mut Verifier::new(&VerifierOptions::default(), value.finished_data()),
            0,
        )
        .is_err()
    {
        return Err(invalid_output(name));
    }
    Ok(value.finished_data().to_vec())
}

pub(crate) fn write_port<'a, T: Verifiable>(
    builder: &mut FlatBufferBuilder<'a>,
    ports: &mut OutputPorts<'a>,
    name: &str,
    pack: impl FnOnce(&mut FlatBufferBuilder<'_>),
) -> Result<()> {
    let bytes = pack_value::<T>(name, pack)?;
    let name = builder.create_string(name);
    let bytes = builder.create_vector(&bytes);
    ports.push(CardPort::create(
        builder,
        &CardPortArgs {
            name: Some(name),
            schema: None,
            bytes: Some(bytes),
        },
    ));
    Ok(())
}

#[doc(hidden)]
#[repr(C)]
pub struct Bytes {
    data: *const u8,
    size: usize,
}

impl Bytes {
    pub(crate) fn from_slice(data: &[u8]) -> Self {
        Self {
            data: data.as_ptr(),
            size: data.len(),
        }
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(transparent)]
pub struct ViewSession(pub [u8; 16]);

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(transparent)]
pub struct Route(pub [u8; 16]);

#[repr(C)]
struct ViewEvent {
    kind: u32,
    route: Route,
    session: ViewSession,
    data: Bytes,
}

type ViewOpenEffect = unsafe extern "C" fn(
    *mut c_void,
    *const c_char,
    u32,
    u32,
    u32,
    *mut Route,
    *mut ViewSession,
) -> c_int;
type RegisterEffect = unsafe extern "C" fn(*mut c_void, *const ViewSession, *mut Route) -> c_int;
type UnregisterEffect = unsafe extern "C" fn(*mut c_void, *const Route) -> c_int;
type SendEffect = unsafe extern "C" fn(*mut c_void, *const ViewSession, *const u8, usize) -> c_int;
type RecvEffect = unsafe extern "C" fn(*mut c_void, *mut ViewEvent, i64) -> c_int;
type CancelEffect = unsafe extern "C" fn(*mut c_void) -> c_int;
type DeadlineEffect = unsafe extern "C" fn(*mut c_void) -> i64;
type CreateArtifact =
    unsafe extern "C" fn(*mut c_void, u32, *mut ArtifactId, *mut Bytes) -> c_int;

/// `TSUGUMI_ARTIFACT_KEEP`: keep the Artifact after its run.
const ARTIFACT_KEEP: u32 = 1;
type OpenArtifact = unsafe extern "C" fn(*mut c_void, *const ArtifactId, *mut Bytes) -> c_int;

#[doc(hidden)]
#[derive(Clone, Copy)]
#[repr(C)]
pub struct Effects {
    view_open: Option<ViewOpenEffect>,
    route_register: Option<RegisterEffect>,
    route_unregister: Option<UnregisterEffect>,
    send: Option<SendEffect>,
    recv: Option<RecvEffect>,
    cancelled: Option<CancelEffect>,
    remaining_ms: Option<DeadlineEffect>,
    artifact_create: Option<CreateArtifact>,
    artifact_open: Option<OpenArtifact>,
}

#[derive(Clone, Copy)]
struct EffectTarget {
    host: *mut c_void,
    effects: Effects,
}

// The host permits effects on Card threads. Call-scoped effects are accessed
// under ACTIVE, and the view callbacks have process lifetime.
unsafe impl Send for EffectTarget {}

static ACTIVE: Mutex<Option<EffectTarget>> = Mutex::new(None);
static DISPLAY: Mutex<Option<EffectTarget>> = Mutex::new(None);
unsafe extern "C" {
    fn tsugumi_rust_result(result: *mut Bytes, data: *const u8, size: usize) -> c_int;
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex.lock().unwrap_or_else(|error| error.into_inner())
}

struct CallScope;

impl CallScope {
    fn enter(host: *mut c_void, effects: Option<Effects>) -> Self {
        if let Some(effects) = effects {
            let target = EffectTarget { host, effects };
            *lock(&DISPLAY) = Some(target);
            *lock(&ACTIVE) = Some(target);
        }
        Self
    }
}

impl Drop for CallScope {
    fn drop(&mut self) {
        *lock(&ACTIVE) = None;
    }
}

pub fn cancelled() -> bool {
    lock(&ACTIVE).as_ref().is_none_or(|call| {
        call.effects
            .cancelled
            .is_none_or(|query| unsafe { query(call.host) != 0 })
    })
}

/// Returns zero between calls and -1 during a call with no deadline.
pub fn remaining_ms() -> i64 {
    lock(&ACTIVE).as_ref().map_or(0, |call| {
        call.effects
            .remaining_ms
            .map_or(0, |query| unsafe { query(call.host) })
    })
}

pub(crate) fn open_view(
    view: &CStr,
    mode: u32,
    width: u32,
    height: u32,
) -> Result<(Route, ViewSession)> {
    let mut route = Route::default();
    let mut session = ViewSession::default();
    let target = *lock(&DISPLAY);
    let status = target.and_then(|t| {
        t.effects.view_open.map(|open| unsafe {
            open(
                t.host,
                view.as_ptr(),
                mode,
                width,
                height,
                &mut route,
                &mut session,
            )
        })
    });
    match status {
        Some(0) => Ok((route, session)),
        _ => Err(effect_failure("open view")),
    }
}

pub(crate) fn register_route(session: &ViewSession) -> Result<Route> {
    let mut route = Route::default();
    let target = *lock(&DISPLAY);
    let status = target.and_then(|t| {
        t.effects
            .route_register
            .map(|register| unsafe { register(t.host, session, &mut route) })
    });
    match status {
        Some(0) => Ok(route),
        _ => Err(effect_failure("register view route")),
    }
}

pub(crate) fn unregister_route(route: &Route) -> Result<()> {
    let target = *lock(&DISPLAY);
    let status = target.and_then(|t| {
        t.effects
            .route_unregister
            .map(|unregister| unsafe { unregister(t.host, route) })
    });
    match status {
        Some(0) => Ok(()),
        _ => Err(effect_failure("unregister view route")),
    }
}

pub(crate) fn send_view(session: &ViewSession, bytes: &[u8]) -> c_int {
    if bytes.len() > (1 << 24) {
        return -1;
    }
    let target = *lock(&DISPLAY);
    target
        .and_then(|t| {
            t.effects
                .send
                .map(|send| unsafe { send(t.host, session, bytes.as_ptr(), bytes.len()) })
        })
        .unwrap_or(-1)
}

pub(crate) fn send_display(session: &ViewSession, bytes: &[u8]) -> c_int {
    const HEADER: usize = 20;
    const PAYLOAD_MAX: usize = (1 << 24) - HEADER;
    unsafe extern "C" {
        fn arc4random_buf(buffer: *mut c_void, size: usize);
    }
    let Some(target) = *lock(&DISPLAY) else {
        return -1;
    };
    let Some(send) = target.effects.send else {
        return -1;
    };
    let Ok(count) = u32::try_from(bytes.len().max(1).div_ceil(PAYLOAD_MAX)) else {
        return -1;
    };
    let mut id = [0u8; 8];
    if count > 1 {
        // The system fills the entire writable ID buffer.
        unsafe { arc4random_buf(id.as_mut_ptr().cast(), id.len()) };
    }
    let mut frame = Vec::with_capacity(HEADER + bytes.len().min(PAYLOAD_MAX));
    for index in 0..count {
        let start = index as usize * PAYLOAD_MAX;
        let payload = &bytes[start..bytes.len().min(start + PAYLOAD_MAX)];
        frame.clear();
        frame.extend_from_slice(&0x4d475354u32.to_le_bytes());
        frame.extend_from_slice(&id);
        for word in [index, count] {
            frame.extend_from_slice(&word.to_le_bytes());
        }
        frame.extend_from_slice(payload);
        let status = unsafe { send(target.host, session, frame.as_ptr(), frame.len()) };
        if status != 0 {
            return status;
        }
    }
    0
}

#[derive(Debug, PartialEq, Eq)]
pub enum Received {
    Message {
        route: Route,
        session: ViewSession,
        data: Vec<u8>,
    },
    Close {
        route: Route,
        session: ViewSession,
    },
    Timeout,
    Cancelled,
}

pub(crate) fn recv_display(timeout_ms: i64) -> Result<Received> {
    let target = *lock(&DISPLAY);
    let mut event = ViewEvent {
        kind: 0,
        route: Route::default(),
        session: ViewSession::default(),
        data: Bytes::from_slice(&[]),
    };
    let status = target
        .and_then(|t| {
            t.effects
                .recv
                .map(|recv| unsafe { recv(t.host, &mut event, timeout_ms) })
        })
        .unwrap_or(-1);
    match status {
        0 if event.kind == 0 => {
            let data = copy_effect_bytes(event.data)
                .ok_or_else(|| effect_failure("receive view message"))?;
            Ok(Received::Message {
                route: event.route,
                session: event.session,
                data,
            })
        }
        0 if event.kind == 1 => Ok(Received::Close {
            route: event.route,
            session: event.session,
        }),
        1 => Ok(Received::Timeout),
        2 => Ok(Received::Cancelled),
        _ => Err(effect_failure("receive display message")),
    }
}

fn copy_effect_bytes(bytes: Bytes) -> Option<Vec<u8>> {
    if bytes.size == 0 {
        return Some(Vec::new());
    }
    if bytes.data.is_null() || bytes.size > isize::MAX as usize {
        return None;
    }
    // The caller is still in the effect's lifetime on this thread.
    Some(unsafe { std::slice::from_raw_parts(bytes.data, bytes.size) }.to_vec())
}

fn effect_failure(action: &str) -> Failure {
    Failure::new(
        FailureKind::worker_failed,
        format!("The host could not {action}"),
    )
}

fn artifact_path(
    failure: impl FnOnce() -> Failure,
    callback: impl FnOnce(&EffectTarget, &mut Bytes) -> Option<i32>,
) -> Result<PathBuf> {
    let target = lock(&ACTIVE);
    let mut bytes = Bytes::from_slice(&[]);
    if target
        .as_ref()
        .and_then(|target| callback(target, &mut bytes))
        != Some(0)
    {
        return Err(failure());
    }
    let Some(path) = copy_effect_bytes(bytes).filter(|path| !path.is_empty() && !path.contains(&0))
    else {
        return Err(failure());
    };
    Ok(PathBuf::from(std::ffi::OsStr::from_bytes(&path)))
}

impl ArtifactT {
    /// A new Artifact kept after its run, and its writable directory.
    pub fn dir() -> Result<(Self, PathBuf)> {
        Self::dir_with_keep(true)
    }

    /// With `keep` false, the Artifact exists only while its run runs.
    pub fn dir_with_keep(keep: bool) -> Result<(Self, PathBuf)> {
        let mut id = ArtifactId::default();
        let flags = if keep { ARTIFACT_KEEP } else { 0 };
        let path = artifact_path(
            || effect_failure("create an Artifact"),
            |target, out| {
                target
                    .effects
                    .artifact_create
                    .map(|create| unsafe { create(target.host, flags, &mut id, out) })
            },
        )?;
        Ok((Self { id: id.unpack() }, path))
    }

    /// A new Artifact kept after its run holding a copy of the file or directory.
    pub fn upload(source: impl AsRef<Path>) -> Result<Self> {
        Self::upload_with_keep(source, true)
    }

    /// With `keep` false, the Artifact exists only while its run runs.
    pub fn upload_with_keep(source: impl AsRef<Path>, keep: bool) -> Result<Self> {
        let source = source.as_ref();
        let (artifact, directory) = Self::dir_with_keep(keep)?;
        if source.is_dir() {
            copy_directory(source, &directory)?;
        } else {
            let name = source
                .file_name()
                .ok_or_else(|| effect_failure("copy the Artifact source"))?;
            std::fs::copy(source, directory.join(name))?;
        }
        Ok(artifact)
    }

    pub fn open(&self) -> Result<PathBuf> {
        open_artifact(&self.id.pack())
    }
}

impl Artifact<'_> {
    pub fn open(&self) -> Result<PathBuf> {
        open_artifact(self.id())
    }
}

fn open_artifact(id: &ArtifactId) -> Result<PathBuf> {
    artifact_path(
        || {
            let hex: String = id.bytes().iter().map(|byte| format!("{byte:02x}")).collect();
            Failure::new(
                FailureKind::worker_failed,
                format!(
                    "artifact {}-{}-{}-{}-{} is not available: it was not kept after its run, \
                     was deleted, or is not in this project",
                    &hex[..8],
                    &hex[8..12],
                    &hex[12..16],
                    &hex[16..20],
                    &hex[20..]
                ),
            )
        },
        |target, out| {
            target
                .effects
                .artifact_open
                .map(|open| unsafe { open(target.host, id, out) })
        },
    )
}

fn copy_directory(source: &Path, destination: &Path) -> std::io::Result<()> {
    for entry in std::fs::read_dir(source)? {
        let entry = entry?;
        let path = entry.path();
        let target = destination.join(entry.file_name());
        let kind = entry.file_type()?;
        if kind.is_symlink() {
            std::os::unix::fs::symlink(std::fs::read_link(path)?, target)?;
        } else if kind.is_dir() {
            std::fs::create_dir(&target)?;
            copy_directory(&path, &target)?;
        } else if kind.is_file() {
            std::fs::copy(path, target)?;
        } else {
            return Err(std::io::Error::new(
                std::io::ErrorKind::Unsupported,
                "Unsupported Artifact source entry",
            ));
        }
    }
    Ok(())
}

fn failure_result(builder: &mut FlatBufferBuilder<'_>, error: Failure) {
    builder.reset();
    let kind = if error.kind != FailureKind::unspecified && error.kind.variant_name().is_some() {
        error.kind
    } else {
        FailureKind::worker_failed
    };
    let message = builder.create_string(&error.message);
    let source = error
        .source
        .as_deref()
        .map(|value| builder.create_string(value));
    let cell = error
        .cell
        .as_deref()
        .map(|value| builder.create_string(value));
    let failure = CardFailure::create(
        builder,
        &CardFailureArgs {
            kind,
            message: Some(message),
            source,
            line: error.line,
            cell,
        },
    );
    let result = CardResult::create(
        builder,
        &CardResultArgs {
            outputs: None,
            failure: Some(failure),
        },
    );
    builder.finish(result, None);
}

#[doc(hidden)]
/// # Safety
/// The C entry must validate the ABI version and provide a writable result,
/// readable input and effects for this call, and invoke the Card serially.
pub unsafe fn invoke<O: PackOutput>(
    input: Bytes,
    host: *mut c_void,
    effects: *const Effects,
    out: *mut Bytes,
    run: impl FnOnce(&[u8]) -> Result<O>,
) -> c_int {
    let invoked = catch_unwind(AssertUnwindSafe(|| {
        let mut builder = FlatBufferBuilder::new();
        let _scope = CallScope::enter(host, unsafe { effects.as_ref().copied() });
        let input = if input.size == 0 || input.data.is_null() || input.size > isize::MAX as usize {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(input.data, input.size) }
        };

        let outcome = catch_unwind(AssertUnwindSafe(|| run(input)));
        match outcome {
            Ok(Ok(output)) => match output.pack(&mut builder) {
                Ok(()) => {}
                Err(error) => failure_result(&mut builder, error),
            },
            Ok(Err(error)) => failure_result(&mut builder, error),
            Err(payload) => {
                let message = payload
                    .downcast_ref::<&str>()
                    .copied()
                    .or_else(|| payload.downcast_ref::<String>().map(String::as_str))
                    .unwrap_or("Card panicked");
                failure_result(
                    &mut builder,
                    Failure::without_location(FailureKind::worker_failed, message),
                );
                // Disposing of a custom panic payload must not prevent delivery
                // of the structured failure. A second payload may also panic.
                if let Err(payload) = catch_unwind(AssertUnwindSafe(|| drop(payload))) {
                    std::mem::forget(payload);
                }
            }
        };
        let bytes = builder.finished_data();
        unsafe { tsugumi_rust_result(out, bytes.as_ptr(), bytes.len()) }
    }));
    match invoked {
        Ok(status) => status,
        Err(payload) => {
            // A user-supplied panic payload could itself panic on drop.
            std::mem::forget(payload);
            1
        }
    }
}

#[doc(hidden)]
pub trait PackOutput {
    fn pack(self, builder: &mut FlatBufferBuilder<'_>) -> Result<()>;
}

pub(crate) fn finish_outputs<'a>(builder: &mut FlatBufferBuilder<'a>, ports: &OutputPorts<'a>) {
    let outputs = builder.create_vector(ports);
    let result = CardResult::create(
        builder,
        &CardResultArgs {
            outputs: Some(outputs),
            failure: None,
        },
    );
    builder.finish(result, None);
}
