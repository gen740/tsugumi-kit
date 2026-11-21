use std::fs::{File, copy};
use std::io::Read;
use std::os::fd::BorrowedFd;
use std::sync::Mutex;
use std::thread::{self, JoinHandle};

use tsugumi::output_types::rust_outputs::summary::{ChoiceT, LabelT};
use tsugumi::{
    Blob, Failure, FailureKind, Image, Inputs, LineChart, Markdown, Module, Output, Plotly, Points,
    Result, Series, Table, VegaLite, cancelled, display, display_with_record, outputs,
    remaining_ms,
};

static BACKGROUND: Mutex<Option<JoinHandle<()>>> = Mutex::new(None);

fn display_samples() {
    let image = [0, 13, 255];
    assert_eq!(display("résumé\n\""), 0);
    assert_eq!(display_with_record(Markdown("**text**"), true), 0);
    assert_eq!(display(2.5), 0);
    assert_eq!(
        display(Table {
            columns: &["n"],
            rows: &[vec!["1"]]
        }),
        0
    );
    assert_eq!(
        display(Image {
            media_type: "image/png",
            bytes: &image,
            alt: "sample"
        }),
        0
    );
    assert_eq!(
        display(Points {
            x: &[1.0],
            y: &[1.0],
            z: &[1.0],
            colors: &[0x123456]
        }),
        0
    );
    assert_eq!(
        display(LineChart {
            series: &[Series {
                id: "id",
                label: "line",
                x: &[2.0],
                y: &[2.0]
            }]
        }),
        0
    );
    assert_eq!(display(Plotly(r#"{"data":[]}"#)), 0);
    assert_eq!(display(VegaLite(r#"{"mark":"point"}"#)), 0);
    let module = Module {
        module: "custom",
        config: r#"{"key":1}"#,
        blobs: &[Blob {
            name: "sample",
            bytes: &image,
        }],
    };
    assert_eq!(display_with_record(module, true), 0);
}

fn run(input: Inputs<'_>) -> Result<Output> {
    match input.params.mode() {
        1 => {
            assert!(
                thread::spawn(|| !cancelled() && remaining_ms() == 500)
                    .join()
                    .unwrap()
            );
            display_samples();
            // The fake host keeps the descriptor open. The Card thread owns
            // its duplicate and can wait until the host returns from the call.
            let fd = unsafe { BorrowedFd::borrow_raw(input.params.fd()) }.try_clone_to_owned()?;
            *BACKGROUND.lock().unwrap() = Some(thread::spawn(move || {
                let mut file = File::from(fd);
                file.read_exact(&mut [0]).unwrap();
                assert!(cancelled());
                assert_eq!(remaining_ms(), 0);
                assert_eq!(display("outside"), 0);
            }));
        }
        2 => BACKGROUND.lock().unwrap().take().unwrap().join().unwrap(),
        3 => {
            return Err(Failure::new(FailureKind::cancelled, "explicit failure").with_cell("fit"));
        }
        4 => panic!("standard exception"),
        5 => std::panic::panic_any(5),
        6 => return Ok(Output::default()),
        8 => {
            assert_eq!(remaining_ms(), -1);
            if cancelled() {
                return Err(Failure::new(FailureKind::cancelled, "host cancelled"));
            }
        }
        _ => {}
    }

    let mut output = Output::default();
    let mut number = outputs::Number::default();
    number.value = input.number.value() + i64::from(input.params.mode() == 9);
    output.number = Some(number);
    if input.params.mode() == 7 {
        return Ok(output);
    }

    let mut summary = outputs::Summary::default();
    let mut label = LabelT::default();
    label.text = input.payload.data_as_label().unwrap().text().to_owned();
    summary.data = ChoiceT::Label(Box::new(label));
    summary.values = input.payload.values().map(|values| values.iter().collect());
    output.summary = Some(summary);

    if let Some(file) = input.file {
        copy(file.value(), "copy.txt")?;
        let mut copied = outputs::Copy::default();
        copied.value = "copy.txt".into();
        output.copy = Some(copied);

        let mut forwarded = outputs::Forwarded::default();
        forwarded.value = file.value().to_owned();
        output.forwarded = Some(forwarded);
    }
    Ok(output)
}

tsugumi::export!(run);
