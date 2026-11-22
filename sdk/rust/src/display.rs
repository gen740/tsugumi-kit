use std::ffi::c_int;

use flatbuffers::FlatBufferBuilder;
use serde_json::{Value, json};

use crate::runtime::{Failure, FailureKind, Result, send_display};
use crate::wire::tsugumi::card::{
    CardDisplay, CardDisplayArgs, CardDisplayBlob, CardDisplayBlobArgs,
};

/// A value that can be sent to a view module.
pub trait DisplayValue {
    fn send(self, record: bool) -> c_int;
}

/// Show a value and choose whether to record it. A rejected display is a Card failure.
#[track_caller]
pub fn display(value: impl DisplayValue, record: bool) -> Result<()> {
    let status = value.send(record);
    if status == 0 {
        Ok(())
    } else {
        Err(Failure::new(
            FailureKind::worker_failed,
            format!("Display failed (status {status})"),
        ))
    }
}

/// Show a value. Recording defaults to false when the second argument is omitted.
#[macro_export]
macro_rules! display {
    ($value:expr $(,)?) => {
        $crate::display($value, false)
    };
    ($value:expr, $record:expr $(,)?) => {
        $crate::display($value, $record)
    };
}

pub struct Blob<'a> {
    pub name: &'a str,
    pub bytes: &'a [u8],
}

pub struct Module<'a> {
    pub module: &'a str,
    pub config: &'a str,
    pub blobs: &'a [Blob<'a>],
}

impl DisplayValue for Module<'_> {
    fn send(self, record: bool) -> c_int {
        let mut builder = FlatBufferBuilder::new();
        let module = builder.create_string(self.module);
        let config = builder.create_string(self.config);
        let blobs: Vec<_> = self
            .blobs
            .iter()
            .map(|blob| {
                let name = builder.create_string(blob.name);
                let bytes = builder.create_vector(blob.bytes);
                CardDisplayBlob::create(
                    &mut builder,
                    &CardDisplayBlobArgs {
                        name: Some(name),
                        bytes: Some(bytes),
                    },
                )
            })
            .collect();
        let blobs = builder.create_vector(&blobs);
        let display = CardDisplay::create(
            &mut builder,
            &CardDisplayArgs {
                module: Some(module),
                config: Some(config),
                blobs: Some(blobs),
            },
        );
        builder.finish(display, None);
        send_display(builder.finished_data(), record)
    }
}

fn builtin(module: &str, config: Value, blobs: &[Blob<'_>], record: bool) -> c_int {
    Module {
        module,
        config: &config.to_string(),
        blobs,
    }
    .send(record)
}

impl DisplayValue for &str {
    fn send(self, record: bool) -> c_int {
        builtin("tsugumi:text", json!({"text": self}), &[], record)
    }
}

impl DisplayValue for String {
    fn send(self, record: bool) -> c_int {
        self.as_str().send(record)
    }
}

pub struct Markdown<'a>(pub &'a str);

impl DisplayValue for Markdown<'_> {
    fn send(self, record: bool) -> c_int {
        builtin("tsugumi:markdown", json!({"text": self.0}), &[], record)
    }
}

/// Format like C's `%.6g`.
fn format_g6(value: f64) -> String {
    let scientific = format!("{value:.5e}");
    let (mantissa, exponent) = scientific.split_once('e').unwrap();
    let exponent: i32 = exponent.parse().unwrap();
    if (-4..6).contains(&exponent) {
        let text = format!("{value:.*}", (5 - exponent) as usize);
        return if text.contains('.') {
            text.trim_end_matches('0').trim_end_matches('.').to_string()
        } else {
            text
        };
    }
    let mantissa = if mantissa.contains('.') {
        mantissa.trim_end_matches('0').trim_end_matches('.')
    } else {
        mantissa
    };
    format!(
        "{mantissa}e{}{:02}",
        if exponent < 0 { '-' } else { '+' },
        exponent.abs()
    )
}

pub struct Scalar<'a> {
    pub value: f64,
    pub label: &'a str,
}

impl DisplayValue for f64 {
    fn send(self, record: bool) -> c_int {
        Scalar {
            value: self,
            label: "",
        }
        .send(record)
    }
}

impl DisplayValue for Scalar<'_> {
    fn send(self, record: bool) -> c_int {
        let number = if self.value.is_finite() {
            format_g6(self.value)
        } else {
            "\u{2014}".to_string()
        };
        let text = if self.label.is_empty() {
            number
        } else {
            format!("**{}**: {number}", self.label)
        };
        Markdown(&text).send(record)
    }
}

pub struct Table<'a, S = &'a str> {
    pub columns: &'a [S],
    pub rows: &'a [Vec<S>],
}

impl<S: AsRef<str>> DisplayValue for Table<'_, S> {
    fn send(self, record: bool) -> c_int {
        let columns: Vec<_> = self.columns.iter().map(AsRef::as_ref).collect();
        let rows: Vec<Vec<_>> = self
            .rows
            .iter()
            .map(|row| row.iter().map(AsRef::as_ref).collect())
            .collect();
        builtin(
            "tsugumi:table",
            json!({"columns": columns, "rows": rows}),
            &[],
            record,
        )
    }
}

pub struct Image<'a> {
    pub media_type: &'a str,
    pub bytes: &'a [u8],
    pub alt: &'a str,
}

impl DisplayValue for Image<'_> {
    fn send(self, record: bool) -> c_int {
        let blobs = [Blob {
            name: "image",
            bytes: self.bytes,
        }];
        builtin(
            "tsugumi:image",
            json!({"media_type": self.media_type, "alt": self.alt}),
            &blobs,
            record,
        )
    }
}

pub struct Points<'a> {
    pub x: &'a [f32],
    pub y: &'a [f32],
    pub z: &'a [f32],
    pub colors: &'a [u32],
}

impl DisplayValue for Points<'_> {
    fn send(self, record: bool) -> c_int {
        let count = self.x.len();
        if self.y.len() != count
            || self.z.len() != count
            || (!self.colors.is_empty() && self.colors.len() != count)
        {
            return 1;
        }

        let x: Vec<u8> = self.x.iter().flat_map(|v| v.to_le_bytes()).collect();
        let y: Vec<u8> = self.y.iter().flat_map(|v| v.to_le_bytes()).collect();
        let z: Vec<u8> = self.z.iter().flat_map(|v| v.to_le_bytes()).collect();
        let colors: Vec<u8> = self.colors.iter().flat_map(|v| v.to_le_bytes()).collect();
        let blobs = [
            Blob {
                name: "x",
                bytes: &x,
            },
            Blob {
                name: "y",
                bytes: &y,
            },
            Blob {
                name: "z",
                bytes: &z,
            },
            Blob {
                name: "colors",
                bytes: &colors,
            },
        ];
        builtin("tsugumi:points", json!({"count": count}), &blobs, record)
    }
}

pub struct Series<'a> {
    pub id: &'a str,
    pub label: &'a str,
    pub x: &'a [f64],
    pub y: &'a [f64],
}

pub struct LineChart<'a> {
    pub series: &'a [Series<'a>],
}

impl DisplayValue for LineChart<'_> {
    fn send(self, record: bool) -> c_int {
        let mut x = Vec::new();
        let mut y = Vec::new();
        let mut offset = 0;
        let mut series = Vec::with_capacity(self.series.len());
        for row in self.series {
            let count = row.x.len();
            if row.y.len() != count || !row.x.iter().chain(row.y).all(|value| value.is_finite()) {
                return 1;
            }
            series
                .push(json!({"id": row.id, "label": row.label, "offset": offset, "count": count}));
            offset += count;
            x.extend(row.x.iter().flat_map(|value| value.to_le_bytes()));
            y.extend(row.y.iter().flat_map(|value| value.to_le_bytes()));
        }
        let blobs = [
            Blob {
                name: "x",
                bytes: &x,
            },
            Blob {
                name: "y",
                bytes: &y,
            },
        ];
        builtin(
            "tsugumi:line-chart",
            json!({"series": series}),
            &blobs,
            record,
        )
    }
}

pub struct Plotly<'a>(pub &'a str);
pub struct VegaLite<'a>(pub &'a str);

impl DisplayValue for Plotly<'_> {
    fn send(self, record: bool) -> c_int {
        Module {
            module: "tsugumi:plotly",
            config: self.0,
            blobs: &[],
        }
        .send(record)
    }
}

impl DisplayValue for VegaLite<'_> {
    fn send(self, record: bool) -> c_int {
        Module {
            module: "tsugumi:vega-lite",
            config: self.0,
            blobs: &[],
        }
        .send(record)
    }
}
