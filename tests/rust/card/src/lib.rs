use tsugumi::input_types::inputs::batch::Element;
use tsugumi::inputs::Arrival;
use tsugumi::output_types::outputs::{ChildT, ChoiceT, ListingT};
use tsugumi::{Artifact, Failure, FailureKind, Inputs, Output, PARAMS, Received, Result};

fn unexpected(message: &str) -> Failure {
    Failure::new(FailureKind::worker_failed, message)
}

fn run(input: Inputs<'_>) -> Result<Output> {
    let (number, granted) = match input.arrival {
        Arrival::Number(value) => (value.value(), value.file()),
        Arrival::Text(value) => (20, value.file()),
    };
    let opened = granted.open()?;
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

    if tsugumi::recv("request", -1)? != Received::Message(vec![42])
        || tsugumi::recv("request", 0)? != Received::Timeout
        || tsugumi::recv("request", 1)? != Received::Cancelled
    {
        return Err(unexpected("Unexpected receive status"));
    }
    tsugumi::send("listing", &[42])?;
    tsugumi::send("listing", &vec![42; (1 << 24) + 5])?;
    std::thread::spawn(|| {
        if let Ok(Received::Message(reply)) = tsugumi::recv("background", -1) {
            let _ = tsugumi::send("background", &reply);
        }
    });

    let mut listing = ListingT::default();
    listing.value = PARAMS.offset + number;
    let mut nested = ChildT::default();
    nested.file = Box::new(artifact);
    listing.nested = Box::new(nested);
    listing.files = vec![upload, granted.unpack()];
    listing.choice = ChoiceT::File(Box::new(Artifact::dir()?.0));
    Ok(Output {
        listing: Some(listing),
        ..Default::default()
    })
}

tsugumi::export!(run);
