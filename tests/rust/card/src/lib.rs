use std::fs::{File, copy};
use std::io::Read;
use std::os::fd::BorrowedFd;
use std::sync::Mutex;
use std::thread::{self, JoinHandle};

use tsugumi::output_types::rust_outputs::summary::{ChoiceT, LabelT};
use tsugumi::{
    Failure, FailureKind, Image, Inputs, LineChart, Markdown, Output, Plotly, Points, Result,
    Series, Table, VegaLite, cancelled, outputs, remaining_ms, send, send_value,
};

static BACKGROUND: Mutex<Option<JoinHandle<()>>> = Mutex::new(None);

fn display_samples() -> Result<()> {
    let image = [0, 13, 255];
    send_value("résumé\n\"")?;
    send_value(Markdown("**text**"))?;
    send_value(2.5)?;
    send_value(Table {
        columns: &["n"],
        rows: &[vec!["1"]],
    })?;
    send_value(Image {
        media_type: "image/png",
        bytes: &image,
        alt: "sample",
    })?;
    send_value(Points {
        x: &[1.0],
        y: &[1.0],
        z: &[1.0],
        colors: &[0x123456],
    })?;
    send_value(LineChart {
        series: &[Series {
            id: "id",
            label: "line",
            x: &[2.0],
            y: &[2.0],
        }],
    })?;
    send_value(Plotly(r#"{"data":[]}"#))?;
    send_value(VegaLite(r#"{"mark":"point"}"#))?;
    send("custom", &image)
}

struct PanicsOnDrop;

impl Drop for PanicsOnDrop {
    fn drop(&mut self) {
        std::panic::panic_any(PanicsOnDrop);
    }
}

fn run(input: Inputs<'_>) -> Result<Output> {
    match input.params.mode() {
        1 => {
            assert!(
                thread::spawn(|| !cancelled() && remaining_ms() == 500)
                    .join()
                    .unwrap()
            );
            display_samples()?;
            // The fake host keeps the descriptor open. The Card thread owns
            // its duplicate and can wait until the host returns from the call.
            let fd = unsafe { BorrowedFd::borrow_raw(input.params.fd()) }.try_clone_to_owned()?;
            *BACKGROUND.lock().unwrap() = Some(thread::spawn(move || {
                let mut file = File::from(fd);
                file.read_exact(&mut [0]).unwrap();
                assert!(cancelled());
                assert_eq!(remaining_ms(), 0);
                send_value("outside").unwrap();
            }));
        }
        2 => BACKGROUND.lock().unwrap().take().unwrap().join().unwrap(),
        3 => {
            return Err(Failure::new(FailureKind::cancelled, "explicit failure").with_cell("fit"));
        }
        4 => panic!("standard exception"),
        5 => std::panic::panic_any(5),
        8 => {
            assert_eq!(remaining_ms(), -1);
            if cancelled() {
                return Err(Failure::new(FailureKind::cancelled, "host cancelled"));
            }
        }
        10 => return Err(Failure::new(FailureKind(u16::MAX), "unknown failure kind")),
        11 => {
            return Err(Failure::new(
                FailureKind::unspecified,
                "reserved failure kind",
            ));
        }
        12 => std::panic::panic_any(PanicsOnDrop),
        13 => send("undeclared", &[0, 13, 255])?,
        14 => send("invalid\0name", &[])?,
        15 => send_value(Points {
            x: &[1.0],
            y: &[],
            z: &[1.0],
            colors: &[],
        })?,
        16 => send_value(LineChart {
            series: &[Series {
                id: "id",
                label: "line",
                x: &[f64::NAN],
                y: &[1.0],
            }],
        })?,
        _ => {}
    }

    assert!(!test_zlib::version().to_bytes().is_empty());
    let position = input.number.position();
    let mut number = outputs::Number::default();
    number.value = position.x() + position.y() + i64::from(input.params.mode() == 9);

    let mut summary = outputs::Summary::default();
    if input.params.mode() != 7 {
        let mut label = LabelT::default();
        label.text = input.payload.data_as_label().unwrap().text().to_owned();
        summary.data = ChoiceT::Label(Box::new(label));
    }
    summary.values = input.payload.values().map(|values| values.iter().collect());

    let mut output = Output {
        number,
        summary,
        copy: None,
        forwarded: None,
        forwarded_directory: None,
        _2d: None,
        async_: None,
    };

    if let Some(file) = input.file {
        copy(file.value(), "copy.txt")?;
        let mut copied = outputs::Copy::default();
        copied.value = "copy.txt".into();
        output.copy = Some(copied);

        let mut forwarded = outputs::Forwarded::default();
        forwarded.value = file.value().to_owned();
        output.forwarded = Some(forwarded);
    }
    if let Some(directory) = input.directory {
        let mut forwarded = outputs::ForwardedDirectory::default();
        forwarded.value = directory.value().to_owned();
        output.forwarded_directory = Some(forwarded);

        let mut point = outputs::Port2d::default();
        point.x = position.x();
        point.y = position.y();
        output._2d = Some(point);

        let mut value = outputs::Async::default();
        value.data = output.summary.data.clone();
        output.async_ = Some(value);
    }
    if input.params.mode() == 6 {
        output.async_ = Some(outputs::Async::default());
    }
    Ok(output)
}

tsugumi::export!(run);
