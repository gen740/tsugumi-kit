use std::collections::HashMap;
use std::ffi::{c_int, c_void};
use std::panic::{AssertUnwindSafe, Location, catch_unwind};
use std::path::{Component, Path};
use std::sync::{Mutex, MutexGuard};

use flatbuffers::{
    FlatBufferBuilder, Follow, ForwardsUOffset, Verifiable, Verifier, VerifierOptions,
};

use crate::wire::tsugumi::card::{
    CardFailure, CardFailureArgs, CardInput, CardPort, CardPortArgs, CardResult, CardResultArgs,
};
pub use crate::wire::tsugumi::data::FailureKind;
pub(crate) use crate::wire::tsugumi::data::PortKind;

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
    params: &'a [u8],
}

impl<'a> Input<'a> {
    pub(crate) fn parse(bytes: &'a [u8]) -> Result<Self> {
        let input = read_root::<CardInput>(bytes, || {
            Failure::without_location(FailureKind::invalid_request, "Invalid CardInput")
        })?;
        let mut ports = HashMap::new();
        for port in input.inputs().into_iter().flatten() {
            if !matches!(
                port.kind(),
                PortKind::FlatBuffer | PortKind::File | PortKind::Directory
            ) {
                return Err(Failure::without_location(
                    FailureKind::invalid_request,
                    "Unknown input port kind",
                ));
            }
            if ports.insert(port.name(), port).is_some() {
                return Err(Failure::without_location(
                    FailureKind::invalid_request,
                    format!("Duplicate input {}", port.name()),
                ));
            }
        }
        Ok(Self {
            ports,
            params: input.params().map_or(&[], |value| value.bytes()),
        })
    }

    pub(crate) fn take<T>(&mut self, name: &str, kind: PortKind) -> Result<Option<T>>
    where
        T: Follow<'a, Inner = T> + Verifiable + 'a,
    {
        let Some(port) = self.ports.remove(name) else {
            return Ok(None);
        };
        if port.kind() != kind {
            return Err(Failure::without_location(
                FailureKind::invalid_request,
                format!("Wrong kind for input {name}"),
            ));
        }
        let bytes: &[u8] = port.bytes().map_or(&[], |value| value.bytes());
        Self::read_value::<T>(bytes, name).map(Some)
    }

    pub(crate) fn required<T>(&mut self, name: &str, kind: PortKind) -> Result<T>
    where
        T: Follow<'a, Inner = T> + Verifiable + 'a,
    {
        self.take(name, kind)?.ok_or_else(|| {
            Failure::without_location(FailureKind::input_missing, format!("Missing input {name}"))
        })
    }

    pub(crate) fn params<T>(self) -> Result<T>
    where
        T: Follow<'a, Inner = T> + Verifiable + 'a,
    {
        if !self.ports.is_empty() {
            return Err(Failure::without_location(
                FailureKind::invalid_request,
                "Unknown input port",
            ));
        }
        Self::read_value::<T>(self.params, "params")
    }

    fn read_value<T>(bytes: &'a [u8], name: &str) -> Result<T>
    where
        T: Follow<'a, Inner = T> + Verifiable + 'a,
    {
        read_root::<T>(bytes, || {
            Failure::without_location(
                FailureKind::invalid_request,
                format!("Invalid FlatBuffer for {name}"),
            )
        })
    }
}

pub(crate) type OutputPorts<'a> = Vec<flatbuffers::WIPOffset<CardPort<'a>>>;

pub(crate) fn write_port<'a, T: Verifiable>(
    builder: &mut FlatBufferBuilder<'a>,
    ports: &mut OutputPorts<'a>,
    name: &str,
    kind: PortKind,
    pack: impl FnOnce(&mut FlatBufferBuilder<'_>),
) -> Result<()> {
    let mut value = FlatBufferBuilder::new();
    // flatc's object API can panic when a required field was left empty.
    if catch_unwind(AssertUnwindSafe(|| pack(&mut value))).is_err()
        || ForwardsUOffset::<T>::run_verifier(
            &mut Verifier::new(&VerifierOptions::default(), value.finished_data()),
            0,
        )
        .is_err()
    {
        return Err(Failure::without_location(
            FailureKind::output_invalid,
            format!("Invalid output {name}"),
        ));
    }

    let name = builder.create_string(name);
    let bytes = builder.create_vector(value.finished_data());
    ports.push(CardPort::create(
        builder,
        &CardPortArgs {
            name: Some(name),
            kind,
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

type DisplayEffect = unsafe extern "C" fn(Bytes, c_int) -> c_int;
type CancelEffect = unsafe extern "C" fn(*mut c_void) -> c_int;
type DeadlineEffect = unsafe extern "C" fn(*mut c_void) -> i64;

#[doc(hidden)]
#[derive(Clone, Copy)]
#[repr(C)]
pub struct Effects {
    display: Option<DisplayEffect>,
    cancelled: Option<CancelEffect>,
    remaining_ms: Option<DeadlineEffect>,
}

struct ActiveCall {
    host: *mut c_void,
    effects: Effects,
}

// The ABI permits queries from any Card thread. The mutex serializes the
// queries and holds the context alive until the call guard can clear it.
unsafe impl Send for ActiveCall {}

static ACTIVE: Mutex<Option<ActiveCall>> = Mutex::new(None);
static DISPLAY: Mutex<Option<DisplayEffect>> = Mutex::new(None);
static RESULT: Mutex<Vec<u8>> = Mutex::new(Vec::new());

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex.lock().unwrap_or_else(|error| error.into_inner())
}

struct CallScope;

impl CallScope {
    fn enter(host: *mut c_void, effects: Option<Effects>) -> Self {
        if let Some(effects) = effects {
            if let Some(callback) = effects.display {
                *lock(&DISPLAY) = Some(callback);
            }
            *lock(&ACTIVE) = Some(ActiveCall { host, effects });
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

pub(crate) fn send_display(bytes: &[u8], record: bool) -> c_int {
    // Release the lock before invoking the process-lifetime callback.
    let callback = *lock(&DISPLAY);
    callback.map_or(1, |send| unsafe {
        send(Bytes::from_slice(bytes), record.into())
    })
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
        let mut result = lock(&RESULT);
        result.clear();
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
        result.extend_from_slice(builder.finished_data());
        unsafe { out.write(Bytes::from_slice(&result)) };
    }));
    if let Err(payload) = invoked {
        // A user-supplied panic payload could itself panic on drop.
        std::mem::forget(payload);
        return 1;
    }
    0
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
