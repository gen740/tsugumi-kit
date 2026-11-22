use std::ffi::CString;

use serde_json::{Value, json};

use crate::runtime::{Failure, FailureKind, Result, send_display};

/// A value that can be sent to a view.
pub trait DisplayValue {
    #[track_caller]
    fn send(self) -> Result<()>;
}

/// Send opaque bytes to a declared view.
#[track_caller]
pub fn send(view: &str, bytes: &[u8]) -> Result<()> {
    let Ok(view) = CString::new(view) else {
        return Err(invalid_display("View names cannot contain a NUL byte"));
    };
    let status = send_display(&view, bytes);
    if status == 0 {
        Ok(())
    } else {
        Err(Failure::new(
            FailureKind::worker_failed,
            format!("Display failed (status {status})"),
        ))
    }
}

/// Encode a typed value and send it to its corresponding view.
#[track_caller]
pub fn send_value(value: impl DisplayValue) -> Result<()> {
    value.send()
}

#[track_caller]
fn invalid_display(message: &str) -> Failure {
    Failure::new(FailureKind::invalid_request, message)
}

#[track_caller]
fn builtin(view: &str, config: Value) -> Result<()> {
    send(view, config.to_string().as_bytes())
}

#[track_caller]
fn with_binary(view: &str, config: Value, parts: &[&[u8]]) -> Result<()> {
    let config = config.to_string();
    let Ok(size) = u32::try_from(config.len()) else {
        return Err(invalid_display("Display metadata is too large"));
    };
    let mut payload = size.to_le_bytes().to_vec();
    payload.extend(config.as_bytes());
    for part in parts {
        payload.extend_from_slice(part);
    }
    send(view, &payload)
}

impl DisplayValue for &str {
    fn send(self) -> Result<()> {
        builtin("text", json!({"text": self}))
    }
}

impl DisplayValue for String {
    fn send(self) -> Result<()> {
        self.as_str().send()
    }
}

pub struct Markdown<'a>(pub &'a str);

impl DisplayValue for Markdown<'_> {
    fn send(self) -> Result<()> {
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
    fn send(self) -> Result<()> {
        Scalar {
            value: self,
            label: "",
        }
        .send()
    }
}

impl DisplayValue for Scalar<'_> {
    fn send(self) -> Result<()> {
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
    fn send(self) -> Result<()> {
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
    fn send(self) -> Result<()> {
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
    fn send(self) -> Result<()> {
        let count = self.x.len();
        if self.y.len() != count
            || self.z.len() != count
            || (!self.colors.is_empty() && self.colors.len() != count)
        {
            return Err(invalid_display(
                "Point coordinates and colors must have matching lengths",
            ));
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
    fn send(self) -> Result<()> {
        let mut x = Vec::new();
        let mut y = Vec::new();
        let mut offset = 0;
        let mut series = Vec::with_capacity(self.series.len());
        for row in self.series {
            let count = row.x.len();
            if row.y.len() != count || !row.x.iter().chain(row.y).all(|value| value.is_finite()) {
                return Err(invalid_display(
                    "Line coordinates must be finite and have matching lengths",
                ));
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
    fn send(self) -> Result<()> {
        send("plotly", self.0.as_bytes())
    }
}

impl DisplayValue for VegaLite<'_> {
    fn send(self) -> Result<()> {
        send("vega-lite", self.0.as_bytes())
    }
}
