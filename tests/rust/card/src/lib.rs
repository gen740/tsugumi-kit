use tsugumi::input_types::inputs::batch::Element;
use tsugumi::inputs::Arrival;
use tsugumi::output_types::outputs::{ChildT, ChoiceT, ListingT};
use tsugumi::{Artifact, Failure, FailureKind, Inputs, Output, PARAMS, Received, Result};

fn unexpected(message: &str) -> Failure {
    Failure::new(FailureKind::worker_failed, message)
}

fn run(input: Inputs<'_>) -> Result<Output> {
    let (number, known) = match input.arrival {
        Arrival::Number(value) => (value.value(), value.file()),
        Arrival::Text(value) => (20, value.file()),
    };
    let opened = known.open()?;
    let items = input
        .batch
        .items()
        .ok_or_else(|| unexpected("Missing items"))?;
    if items.len() != 1 || input.direct.open()? != opened {
        return Err(unexpected("Unexpected Artifact or Collect input"));
    }
    let envelope = items.get(0);
    let member = envelope
        .bytes()
        .ok_or_else(|| unexpected("Missing member bytes"))?;
    let element = tsugumi::flatbuffers::root::<Element>(member.bytes())
        .map_err(|_| unexpected("Invalid Collect member"))?;
    if envelope.schema() != Some("4444444444444444444444444444444444444444444444444444444444444444")
        || element.value() != 4
        || !PARAMS.flag
        || PARAMS.ratio != 1.0
        || PARAMS.labels[0] != "日本語"
        || tsugumi::cancelled()
        || tsugumi::remaining_ms() != 500
        || test_zlib::version().to_bytes().is_empty()
    {
        return Err(unexpected("Unexpected contract fixture values"));
    }
    if number < 0 {
        return Ok(Output::default());
    }
    let (artifact, directory) = Artifact::dir()?;
    let file = directory.join("value.txt");
    std::fs::write(&file, "test")?;
    let upload = Artifact::upload(file)?;

    let (opener, session) = tsugumi::view_open(
        "listing",
        tsugumi::ViewMode::Window,
        tsugumi::VIEW_HIDE_CURSOR | tsugumi::VIEW_LOCK_POINTER,
        640,
        480,
    )?;
    let route = tsugumi::route_register(&session)?;
    if tsugumi::recv(-1)?
        != (Received::Message {
            route,
            session,
            data: vec![42],
        })
        || tsugumi::recv(0)? != Received::Timeout
        || tsugumi::recv(1)? != Received::Cancelled
    {
        return Err(unexpected("Unexpected receive status"));
    }
    tsugumi::route_unregister(&route)?;
    tsugumi::send(&session, &[42])?;
    tsugumi::send(&session, &vec![42; 1 << 24])?;
    if tsugumi::send(&session, &vec![42; (1 << 24) + 5]).is_ok() {
        return Err(unexpected("Oversize message was accepted"));
    }
    tsugumi::view_close(&session)?;
    if !matches!(tsugumi::route_register(&session), Err(error) if error.kind == FailureKind::not_found)
        || tsugumi::recv(-1)?
            != (Received::Close {
                route: opener,
                session,
            })
    {
        return Err(unexpected("Unexpected send or close status"));
    }
    std::thread::spawn(|| {
        if let Ok(Received::Message { data, .. }) = tsugumi::recv(-1) {
            let (_, session) = tsugumi::view_open("background", tsugumi::ViewMode::Panel, 0, 0, 0)
                .expect("Open background view");
            let _ = tsugumi::send(&session, &data);
        }
    });

    let mut listing = ListingT::default();
    listing.value = PARAMS.offset + number;
    let mut nested = ChildT::default();
    nested.file = Box::new(artifact);
    listing.nested = Box::new(nested);
    listing.files = vec![upload, known.unpack()];
    listing.choice = ChoiceT::File(Box::new(Artifact::dir_with_keep(false)?.0));
    Ok(Output {
        listing: Some(listing),
        ..Default::default()
    })
}

tsugumi::export!(run);
