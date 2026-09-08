//! `CefDialogHandler` — file choosers for a windowless browser.
//!
//! Jellium Desktop runs CEF windowless (OSR), so CEF's default file
//! chooser has no aura window to parent onto:
//! `FileSelectHelper::RunFileChooserOnUIThread` walks
//! `aura::Window::GetToplevelWindow()` and faults (0xc0000005 inside
//! `libcef.dll`), and newer builds instead log "Default dialog implementation
//! is not available; canceling the file dialog". Either way an
//! `<input type=file>` in jellyfin-web is unusable (#681).
//!
//! `on_file_dialog` therefore ALWAYS claims the dialog (returns 1) so the
//! default path never runs, and hands a [`FileDialogRequest`] to the platform
//! backend. Backends with no native chooser return `false` and the CEF
//! callback is cancelled right here — graceful, never a crash.

use cef::rc::Rc;
use cef::*;
use std::os::raw::c_int;
use std::path::PathBuf;
use std::sync::Arc;

use crate::client::Inner;
use crate::platform_ops::{FileDialogFilter, FileDialogKind, FileDialogRequest};

/// Extensions used when a page accepts a whole MIME group.
const IMAGE_EXTENSIONS: &[&str] = &[
    "jpg", "jpeg", "png", "gif", "webp", "bmp", "avif", "ico", "svg", "tif", "tiff", "heic",
];
const VIDEO_EXTENSIONS: &[&str] = &[
    "mp4", "m4v", "mkv", "webm", "mov", "avi", "wmv", "flv", "mpg", "mpeg", "ts", "m2ts",
];
const AUDIO_EXTENSIONS: &[&str] = &[
    "mp3", "m4a", "aac", "flac", "wav", "ogg", "oga", "opus", "wma",
];

/// Extensions for the handful of concrete MIME types a Jellyfin page asks for.
fn mime_extensions(mime: &str) -> &'static [&'static str] {
    match mime {
        "image/jpeg" | "image/jpg" => &["jpg", "jpeg"],
        "image/png" => &["png"],
        "image/gif" => &["gif"],
        "image/webp" => &["webp"],
        "image/bmp" | "image/x-ms-bmp" => &["bmp"],
        "image/avif" => &["avif"],
        "image/svg+xml" => &["svg"],
        "image/tiff" => &["tif", "tiff"],
        "image/x-icon" | "image/vnd.microsoft.icon" => &["ico"],
        "video/mp4" => &["mp4", "m4v"],
        "video/x-matroska" => &["mkv"],
        "video/webm" => &["webm"],
        "video/quicktime" => &["mov"],
        "audio/mpeg" | "audio/mp3" => &["mp3"],
        "audio/mp4" => &["m4a"],
        "audio/aac" => &["aac"],
        "audio/flac" | "audio/x-flac" => &["flac"],
        "audio/wav" | "audio/x-wav" => &["wav"],
        "audio/ogg" => &["ogg", "oga"],
        "text/plain" => &["txt"],
        "text/vtt" => &["vtt"],
        "application/json" => &["json"],
        "application/pdf" => &["pdf"],
        "application/zip" => &["zip"],
        "application/x-subrip" | "application/x-srt" => &["srt"],
        _ => &[],
    }
}

/// `".JPG"` / `"jpg"` / `"*.jpg"` all normalize to `"jpg"`; anything with a
/// path separator or wildcard left in it is rejected.
fn normalize_extension(raw: &str) -> Option<String> {
    let ext = raw
        .trim()
        .trim_start_matches('*')
        .trim_start_matches('.')
        .to_ascii_lowercase();
    if ext.is_empty() || ext.contains(['/', '\\', '*', '?', ';', ' ']) {
        return None;
    }
    Some(ext)
}

fn push_unique(out: &mut Vec<String>, ext: String) {
    if !out.contains(&ext) {
        out.push(ext);
    }
}

/// `"JPG, JPEG"` — the label used when CEF supplied no description.
fn describe(extensions: &[String]) -> String {
    extensions
        .iter()
        .map(|e| e.to_ascii_uppercase())
        .collect::<Vec<_>>()
        .join(", ")
}

/// Builds the dialog's type dropdown.
///
/// `accept_extensions` / `accept_descriptions` are CEF's parallel lists — one
/// entry per filter, extensions given as `".jpg;.jpeg"`. When they are empty
/// (older CEF, or a filter set CEF could not expand) the raw `accept_filters`
/// are parsed instead: `".png"` style extensions collapse into one bucket and
/// MIME types map to their extension sets.
pub(crate) fn filters_from_accept(
    accept_filters: &[String],
    accept_extensions: &[String],
    accept_descriptions: &[String],
) -> Vec<FileDialogFilter> {
    let mut out: Vec<FileDialogFilter> = Vec::new();

    if !accept_extensions.is_empty() {
        for (i, group) in accept_extensions.iter().enumerate() {
            let mut extensions = Vec::new();
            for raw in group.split(';') {
                if let Some(ext) = normalize_extension(raw) {
                    push_unique(&mut extensions, ext);
                }
            }
            if extensions.is_empty() {
                continue;
            }
            let description = accept_descriptions
                .get(i)
                .map(|d| d.trim())
                .filter(|d| !d.is_empty())
                .map(str::to_string)
                .unwrap_or_else(|| describe(&extensions));
            out.push(FileDialogFilter {
                description,
                extensions,
            });
        }
        if !out.is_empty() {
            return out;
        }
    }

    let mut loose: Vec<String> = Vec::new();
    for raw in accept_filters {
        let entry = raw.trim().to_ascii_lowercase();
        if entry.is_empty() {
            continue;
        }
        let group = match entry.as_str() {
            "image/*" => Some(("Image files", IMAGE_EXTENSIONS)),
            "video/*" => Some(("Video files", VIDEO_EXTENSIONS)),
            "audio/*" => Some(("Audio files", AUDIO_EXTENSIONS)),
            _ => None,
        };
        if let Some((description, extensions)) = group {
            if out.iter().any(|f| f.description == description) {
                continue;
            }
            out.push(FileDialogFilter {
                description: description.to_string(),
                extensions: extensions.iter().map(|e| (*e).to_string()).collect(),
            });
            continue;
        }
        let mapped = mime_extensions(&entry);
        if !mapped.is_empty() {
            for ext in mapped {
                push_unique(&mut loose, (*ext).to_string());
            }
            continue;
        }
        if entry.contains('/') {
            // Unknown MIME type: the subtype is the best extension guess.
            if let Some(sub) = entry.rsplit('/').next().and_then(normalize_extension) {
                push_unique(&mut loose, sub);
            }
            continue;
        }
        if let Some(ext) = normalize_extension(&entry) {
            push_unique(&mut loose, ext);
        }
    }
    if !loose.is_empty() {
        let description = describe(&loose);
        out.push(FileDialogFilter {
            description,
            extensions: loose,
        });
    }
    out
}

fn kind_from_mode(mode: FileDialogMode) -> FileDialogKind {
    if mode == FileDialogMode::OPEN_MULTIPLE {
        FileDialogKind::OpenFiles
    } else if mode == FileDialogMode::OPEN_FOLDER {
        FileDialogKind::OpenFolder
    } else if mode == FileDialogMode::SAVE {
        FileDialogKind::SaveFile
    } else {
        FileDialogKind::OpenFile
    }
}

fn mode_name(kind: FileDialogKind) -> &'static str {
    match kind {
        FileDialogKind::OpenFile => "open",
        FileDialogKind::OpenFiles => "open-multiple",
        FileDialogKind::OpenFolder => "open-folder",
        FileDialogKind::SaveFile => "save",
    }
}

/// Snapshot a CEF-owned string list without taking ownership of it: the
/// borrowed view built from the raw handle never frees the caller's list.
fn read_list(list: Option<&mut CefStringList>) -> Vec<String> {
    let Some(list) = list else {
        return Vec::new();
    };
    let raw: *mut sys::_cef_string_list_t = list.into();
    if raw.is_null() {
        return Vec::new();
    }
    CefStringList::from(raw).into_iter().collect()
}

fn non_empty(s: Option<&CefString>) -> Option<String> {
    let text = s?.to_string();
    (!text.is_empty()).then_some(text)
}

fn log_line(level: u8, msg: String) {
    jfn_logging::log(jfn_logging::CATEGORY_CEF, level, &msg);
}

/// `FileDialogCallback` is a refcounted CEF object; the handle is only moved
/// to the dialog thread and back, and is used solely on TID_UI.
struct SendCallback(FileDialogCallback);

// SAFETY: mirrors the crate's existing treatment of CEF callback handles
// (`Inner` holds a `RunContextMenuCallback` across threads). The refcount is
// atomic, so moving the handle is sound; every call on it is made from the
// posted TID_UI task.
unsafe impl Send for SendCallback {}

impl Clone for SendCallback {
    fn clone(&self) -> Self {
        SendCallback(self.0.clone())
    }
}

wrap_task! {
    struct FileDialogResultTask {
        callback: SendCallback,
        paths: Option<Vec<PathBuf>>,
    }
    impl Task {
        fn execute(&self) {
            let picked: Vec<String> = self
                .paths
                .as_ref()
                .map(|paths| {
                    paths
                        .iter()
                        .map(|p| p.to_string_lossy().into_owned())
                        .filter(|p| !p.is_empty())
                        .collect()
                })
                .unwrap_or_default();
            if picked.is_empty() {
                log_line(jfn_logging::LEVEL_INFO, "file dialog: cancelled".to_string());
                self.callback.0.cancel();
                return;
            }
            log_line(
                jfn_logging::LEVEL_INFO,
                format!("file dialog: {} path(s) chosen", picked.len()),
            );
            let mut list = CefStringList::new();
            for path in &picked {
                list.append(path);
            }
            self.callback.0.cont(Some(&mut list));
        }
    }
}

/// Delivers the backend's answer on TID_UI, where CEF requires it.
fn deliver(callback: SendCallback, paths: Option<Vec<PathBuf>>) {
    let mut task = FileDialogResultTask::new(callback, paths);
    if post_task(ThreadId::UI, Some(&mut task)) == 0 {
        log_line(
            jfn_logging::LEVEL_WARN,
            "file dialog: TID_UI post refused; result dropped".to_string(),
        );
    }
}

wrap_dialog_handler! {
    pub struct JfnDialogHandlerBuilder {
        inner: Arc<Inner>,
    }

    impl DialogHandler {
        fn on_file_dialog(
            &self,
            _browser: Option<&mut Browser>,
            mode: FileDialogMode,
            title: Option<&CefString>,
            default_file_path: Option<&CefString>,
            accept_filters: Option<&mut CefStringList>,
            accept_extensions: Option<&mut CefStringList>,
            accept_descriptions: Option<&mut CefStringList>,
            callback: Option<&mut FileDialogCallback>,
        ) -> c_int {
            // Claimed unconditionally: CEF's default chooser crashes an OSR
            // browser, so it must never run.
            let Some(callback) = callback else {
                return 1;
            };
            let kind = kind_from_mode(mode);
            let filters = filters_from_accept(
                &read_list(accept_filters),
                &read_list(accept_extensions),
                &read_list(accept_descriptions),
            );
            log_line(
                jfn_logging::LEVEL_INFO,
                format!(
                    "file dialog: mode={} filters={}",
                    mode_name(kind),
                    filters.len()
                ),
            );

            let Some(ops) = crate::platform_ops::ops() else {
                log_line(
                    jfn_logging::LEVEL_WARN,
                    "file dialog: no platform backend; cancelled".to_string(),
                );
                callback.cancel();
                return 1;
            };

            let sink = SendCallback(callback.clone());
            let req = FileDialogRequest {
                kind,
                title: non_empty(title),
                default_path: non_empty(default_file_path).map(PathBuf::from),
                filters,
                on_done: Box::new(move |paths| deliver(sink, paths)),
            };
            if !ops.open_file_dialog(req) {
                log_line(
                    jfn_logging::LEVEL_INFO,
                    "file dialog: unsupported on this platform; cancelled".to_string(),
                );
                callback.cancel();
            }
            1
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn strings(v: &[&str]) -> Vec<String> {
        v.iter().map(|s| (*s).to_string()).collect()
    }

    #[test]
    fn parallel_lists_win_over_raw_filters() {
        let f = filters_from_accept(
            &strings(&["image/*"]),
            &strings(&[".jpg;.jpeg", ".png"]),
            &strings(&["JPEG Image", "PNG Image"]),
        );
        assert_eq!(f.len(), 2);
        assert_eq!(f[0].description, "JPEG Image");
        assert_eq!(f[0].extensions, strings(&["jpg", "jpeg"]));
        assert_eq!(f[1].description, "PNG Image");
        assert_eq!(f[1].extensions, strings(&["png"]));
    }

    #[test]
    fn missing_description_is_derived_from_extensions() {
        let f = filters_from_accept(&[], &strings(&[".jpg;.jpeg"]), &strings(&["  "]));
        assert_eq!(f.len(), 1);
        assert_eq!(f[0].description, "JPG, JPEG");
    }

    #[test]
    fn extensions_are_normalized_and_deduped() {
        let f = filters_from_accept(&[], &strings(&["*.JPG;.jpg;;.Jpeg"]), &[]);
        assert_eq!(f[0].extensions, strings(&["jpg", "jpeg"]));
    }

    #[test]
    fn wildcard_mime_expands_to_a_group() {
        let f = filters_from_accept(&strings(&["image/*"]), &[], &[]);
        assert_eq!(f.len(), 1);
        assert_eq!(f[0].description, "Image files");
        assert!(f[0].extensions.contains(&"png".to_string()));
        assert!(f[0].extensions.contains(&"jpg".to_string()));
    }

    #[test]
    fn repeated_wildcard_mime_yields_one_group() {
        let f = filters_from_accept(&strings(&["image/*", "IMAGE/*"]), &[], &[]);
        assert_eq!(f.len(), 1);
    }

    #[test]
    fn bare_extensions_collapse_into_one_bucket() {
        let f = filters_from_accept(&strings(&[".png", ".jpg", ".png"]), &[], &[]);
        assert_eq!(f.len(), 1);
        assert_eq!(f[0].extensions, strings(&["png", "jpg"]));
        assert_eq!(f[0].description, "PNG, JPG");
    }

    #[test]
    fn concrete_mime_types_map_to_extensions() {
        let f = filters_from_accept(&strings(&["image/jpeg", "image/png"]), &[], &[]);
        assert_eq!(f.len(), 1);
        assert_eq!(f[0].extensions, strings(&["jpg", "jpeg", "png"]));
    }

    #[test]
    fn unknown_mime_falls_back_to_its_subtype() {
        let f = filters_from_accept(&strings(&["application/x-thing"]), &[], &[]);
        assert_eq!(f[0].extensions, strings(&["x-thing"]));
    }

    #[test]
    fn groups_and_loose_extensions_coexist() {
        let f = filters_from_accept(&strings(&["video/*", ".srt"]), &[], &[]);
        assert_eq!(f.len(), 2);
        assert_eq!(f[0].description, "Video files");
        assert_eq!(f[1].extensions, strings(&["srt"]));
    }

    #[test]
    fn empty_accept_yields_no_filters() {
        assert!(filters_from_accept(&[], &[], &[]).is_empty());
        assert!(filters_from_accept(&strings(&["", "   "]), &[], &[]).is_empty());
    }

    #[test]
    fn extension_groups_with_nothing_usable_fall_back_to_filters() {
        let f = filters_from_accept(&strings(&["image/*"]), &strings(&["", " "]), &[]);
        assert_eq!(f.len(), 1);
        assert_eq!(f[0].description, "Image files");
    }

    #[test]
    fn mode_mapping_covers_every_cef_mode() {
        assert_eq!(
            kind_from_mode(FileDialogMode::OPEN),
            FileDialogKind::OpenFile
        );
        assert_eq!(
            kind_from_mode(FileDialogMode::OPEN_MULTIPLE),
            FileDialogKind::OpenFiles
        );
        assert_eq!(
            kind_from_mode(FileDialogMode::OPEN_FOLDER),
            FileDialogKind::OpenFolder
        );
        assert_eq!(
            kind_from_mode(FileDialogMode::SAVE),
            FileDialogKind::SaveFile
        );
    }
}
