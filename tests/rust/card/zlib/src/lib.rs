use std::ffi::{CStr, c_char};

unsafe extern "C" {
    fn zlibVersion() -> *const c_char;
}

pub fn version() -> &'static CStr {
    // zlib returns a pointer to its static, NUL-terminated version string.
    unsafe { CStr::from_ptr(zlibVersion()) }
}
