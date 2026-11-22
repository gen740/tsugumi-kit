use std::ffi::c_int;

use serde_json::{Value, json};

use crate::runtime::{Failure, FailureKind, Result, send_display};

/// A value that can be sent to a view.
pub trait DisplayValue {
    fn send(self) -> c_int;
}

/// Send opaque bytes to a declared view.
#[track_caller]
pub fn send(view: &str, bytes: &[u8]) -> Result<()> {
    check_status(send_display(view, bytes))
}

/// Encode a typed value and send it to its corresponding view.
#[track_caller]
pub fn send_value(value: impl DisplayValue) -> Result<()> {
    check_status(value.send())
}

#[track_caller]
fn check_status(status: c_int) -> Result<()> {
    if status == 0 {
        Ok(())
    } else {
        Err(Failure::new(
            FailureKind::worker_failed,
            format!("Display failed (status {status})"),
        ))
    }
}

fn builtin(view: &str, config: Value) -> c_int {
    send_display(view, config.to_string().as_bytes())
}

fn with_binary(view: &str, config: Value, parts: &[&[u8]]) -> c_int {
    let config = config.to_string();
    let Ok(size) = u32::try_from(config.len()) else {
        return 1;
    };
    let mut payload = size.to_le_bytes().to_vec();
    payload.extend(config.as_bytes());
    for part in parts {
        payload.extend_from_slice(part);
    }
    send_display(view, &payload)
}

impl DisplayValue for &str {
    fn send(self) -> c_int {
        builtin("text", json!({"text": self}))
    }
}

impl DisplayValue for String {
    fn send(self) -> c_int {
        self.as_str().send()
    }
}

pub struct Markdown<'a>(pub &'a str);

impl DisplayValue for Markdown<'_> {
    fn send(self) -> c_int {
        builtin("markdown", json!({"text": self.0}))
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
    fn send(self) -> c_int {
        Scalar {
            value: self,
            label: "",
        }
        .send()
    }
}

impl DisplayValue for Scalar<'_> {
    fn send(self) -> c_int {
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
        Markdown(&text).send()
    }
}

pub struct Table<'a, S = &'a str> {
    pub columns: &'a [S],
    pub rows: &'a [Vec<S>],
}

impl<S: AsRef<str>> DisplayValue for Table<'_, S> {
    fn send(self) -> c_int {
        let columns: Vec<_> = self.columns.iter().map(AsRef::as_ref).collect();
        let rows: Vec<Vec<_>> = self
            .rows
            .iter()
            .map(|row| row.iter().map(AsRef::as_ref).collect())
            .collect();
        builtin("table", json!({"columns": columns, "rows": rows}))
    }
}

pub struct Image<'a> {
    pub media_type: &'a str,
    pub bytes: &'a [u8],
    pub alt: &'a str,
}

impl DisplayValue for Image<'_> {
    fn send(self) -> c_int {
        with_binary(
            "image",
            json!({"media_type": self.media_type, "alt": self.alt}),
            &[self.bytes],
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
    fn send(self) -> c_int {
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
        with_binary("points", json!({"count": count}), &[&x, &y, &z, &colors])
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
    fn send(self) -> c_int {
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
        with_binary("line-chart", json!({"series": series}), &[&x, &y])
    }
}

pub struct Plotly<'a>(pub &'a str);
pub struct VegaLite<'a>(pub &'a str);

impl DisplayValue for Plotly<'_> {
    fn send(self) -> c_int {
        send_display("plotly", self.0.as_bytes())
    }
}

impl DisplayValue for VegaLite<'_> {
    fn send(self) -> c_int {
        send_display("vega-lite", self.0.as_bytes())
    }
}
