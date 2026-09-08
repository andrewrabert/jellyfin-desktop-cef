//! Windows file choosers — the Common Item Dialog on a dedicated thread.
//!
//! `IFileDialog::Show` runs a modal message loop for as long as the dialog is
//! up, so it can run on neither the CEF UI thread (which would stall the
//! browser) nor the Win32 input thread (which would stall mpv's window). Each
//! request gets its own apartment-threaded COM thread that lives exactly as
//! long as the dialog; the result is handed back through `on_done`.

use std::ffi::OsStr;
use std::os::windows::ffi::OsStrExt;
use std::path::{Path, PathBuf};

use jfn_platform_abi::{FileDialogFilter, FileDialogKind, FileDialogRequest};
use windows::Win32::Foundation::{ERROR_CANCELLED, HWND};
use windows::Win32::System::Com::{
    CLSCTX_INPROC_SERVER, COINIT_APARTMENTTHREADED, CoCreateInstance, CoInitializeEx,
    CoTaskMemFree, CoUninitialize,
};
use windows::Win32::UI::Shell::Common::COMDLG_FILTERSPEC;
use windows::Win32::UI::Shell::{
    FILEOPENDIALOGOPTIONS, FOS_ALLOWMULTISELECT, FOS_FORCEFILESYSTEM, FOS_OVERWRITEPROMPT,
    FOS_PATHMUSTEXIST, FOS_PICKFOLDERS, FileOpenDialog, FileSaveDialog, IFileDialog,
    IFileOpenDialog, IShellItem, SHCreateItemFromParsingName, SIGDN_FILESYSPATH,
};
use windows::core::{HRESULT, Interface, PCWSTR};

/// Trailing entry so a user can always reach a file the filters exclude.
const ALL_FILES: (&str, &str) = ("All files", "*.*");

/// The `(name, spec)` pair for one `COMDLG_FILTERSPEC`.
///
/// `spec` is the semicolon-joined glob list the dialog matches on; `name` is
/// what the type dropdown shows, with the globs appended so the user can see
/// what an entry actually covers.
fn filter_spec(filter: &FileDialogFilter) -> Option<(String, String)> {
    let spec = filter
        .extensions
        .iter()
        .filter(|e| !e.is_empty())
        .map(|e| format!("*.{e}"))
        .collect::<Vec<_>>()
        .join(";");
    if spec.is_empty() {
        return None;
    }
    let description = filter.description.trim();
    let name = if description.is_empty() {
        spec.clone()
    } else {
        format!("{description} ({spec})")
    };
    Some((name, spec))
}

/// Every dropdown entry for `filters`, always ending in "All files (*.*)".
fn filter_specs(filters: &[FileDialogFilter]) -> Vec<(String, String)> {
    let mut out: Vec<(String, String)> = filters.iter().filter_map(filter_spec).collect();
    out.push((
        format!("{} ({})", ALL_FILES.0, ALL_FILES.1),
        ALL_FILES.1.to_string(),
    ));
    out
}

fn wide(s: &str) -> Vec<u16> {
    OsStr::new(s)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect()
}

fn options_for(kind: FileDialogKind) -> FILEOPENDIALOGOPTIONS {
    let mut opts = FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    match kind {
        FileDialogKind::OpenFile => {}
        FileDialogKind::OpenFiles => opts |= FOS_ALLOWMULTISELECT,
        FileDialogKind::OpenFolder => opts |= FOS_PICKFOLDERS,
        FileDialogKind::SaveFile => opts |= FOS_OVERWRITEPROMPT,
    }
    opts
}

/// The folder to start in and the file name to prefill, from `default_path`.
///
/// A path that names an existing directory seeds the folder; anything else is
/// treated as `<folder>/<name>`.
fn seed_from(default_path: &Path) -> (Option<PathBuf>, Option<String>) {
    if default_path.is_dir() {
        return (Some(default_path.to_path_buf()), None);
    }
    let folder = default_path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .map(Path::to_path_buf);
    let name = default_path
        .file_name()
        .map(|n| n.to_string_lossy().into_owned());
    (folder, name)
}

/// `IShellItem` → filesystem path. The display name is CoTaskMem-allocated.
fn item_path(item: &IShellItem) -> Option<PathBuf> {
    let raw = match unsafe { item.GetDisplayName(SIGDN_FILESYSPATH) } {
        Ok(raw) => raw,
        Err(e) => {
            tracing::warn!("file dialog: GetDisplayName failed: {e:?}");
            return None;
        }
    };
    if raw.is_null() {
        return None;
    }
    let text = String::from_utf16_lossy(unsafe { raw.as_wide() });
    unsafe { CoTaskMemFree(Some(raw.0.cast())) };
    (!text.is_empty()).then(|| PathBuf::from(text))
}

fn cancelled(e: &windows::core::Error) -> bool {
    e.code() == HRESULT::from_win32(ERROR_CANCELLED.0)
}

/// Builds, shows and drains the dialog. `Ok(None)` is a user cancel.
fn show(
    kind: FileDialogKind,
    title: Option<&str>,
    default_path: Option<&Path>,
    filters: &[FileDialogFilter],
    owner: Option<HWND>,
) -> windows::core::Result<Option<Vec<PathBuf>>> {
    let save = kind == FileDialogKind::SaveFile;
    let clsid = if save {
        &FileSaveDialog
    } else {
        &FileOpenDialog
    };
    let dialog: IFileDialog = unsafe { CoCreateInstance(clsid, None, CLSCTX_INPROC_SERVER) }?;

    let existing = unsafe { dialog.GetOptions() }?;
    unsafe { dialog.SetOptions(existing | options_for(kind)) }?;

    if let Some(title) = title {
        let title = wide(title);
        // A failed title is cosmetic; never abandon the dialog over it.
        if let Err(e) = unsafe { dialog.SetTitle(PCWSTR::from_raw(title.as_ptr())) } {
            tracing::warn!("file dialog: SetTitle failed: {e:?}");
        }
    }

    // Folder pickers have no file types, and the specs must outlive the call.
    let specs = (kind != FileDialogKind::OpenFolder).then(|| filter_specs(filters));
    if let Some(specs) = specs.as_ref() {
        let wide_specs: Vec<(Vec<u16>, Vec<u16>)> = specs
            .iter()
            .map(|(name, spec)| (wide(name), wide(spec)))
            .collect();
        let raw: Vec<COMDLG_FILTERSPEC> = wide_specs
            .iter()
            .map(|(name, spec)| COMDLG_FILTERSPEC {
                pszName: PCWSTR::from_raw(name.as_ptr()),
                pszSpec: PCWSTR::from_raw(spec.as_ptr()),
            })
            .collect();
        if let Err(e) = unsafe { dialog.SetFileTypes(&raw) } {
            tracing::warn!("file dialog: SetFileTypes failed: {e:?}");
        }
    }

    if let Some(default_path) = default_path {
        let (folder, name) = seed_from(default_path);
        if let Some(folder) = folder {
            let folder = wide(&folder.to_string_lossy());
            match unsafe {
                SHCreateItemFromParsingName::<_, _, IShellItem>(
                    PCWSTR::from_raw(folder.as_ptr()),
                    None,
                )
            } {
                Ok(item) => {
                    if let Err(e) = unsafe { dialog.SetFolder(&item) } {
                        tracing::warn!("file dialog: SetFolder failed: {e:?}");
                    }
                }
                Err(e) => tracing::debug!("file dialog: default folder unusable: {e:?}"),
            }
        }
        if let Some(name) = name {
            let name = wide(&name);
            if let Err(e) = unsafe { dialog.SetFileName(PCWSTR::from_raw(name.as_ptr())) } {
                tracing::warn!("file dialog: SetFileName failed: {e:?}");
            }
        }
    }

    if let Err(e) = unsafe { dialog.Show(owner) } {
        if cancelled(&e) {
            return Ok(None);
        }
        return Err(e);
    }

    if save || kind == FileDialogKind::OpenFile || kind == FileDialogKind::OpenFolder {
        let item = unsafe { dialog.GetResult() }?;
        return Ok(item_path(&item).map(|p| vec![p]));
    }

    let open: IFileOpenDialog = dialog.cast()?;
    let items = unsafe { open.GetResults() }?;
    let count = unsafe { items.GetCount() }?;
    let mut paths = Vec::with_capacity(count as usize);
    for i in 0..count {
        let item = unsafe { items.GetItemAt(i) }?;
        if let Some(path) = item_path(&item) {
            paths.push(path);
        }
    }
    Ok((!paths.is_empty()).then_some(paths))
}

/// Runs one dialog to completion on the thread this is spawned onto.
///
/// The owner window arrives as a raw handle value because `HWND` is not
/// `Send`; it is only ever passed straight back to `Show`.
fn run(req: FileDialogRequest, owner_raw: usize) {
    let owner = (owner_raw != 0).then_some(HWND(owner_raw as *mut std::ffi::c_void));
    let FileDialogRequest {
        kind,
        title,
        default_path,
        filters,
        on_done,
    } = req;

    let hr = unsafe { CoInitializeEx(None, COINIT_APARTMENTTHREADED) };
    // S_OK and S_FALSE both leave an initialized apartment to balance.
    let initialized = hr.is_ok();
    if !initialized {
        tracing::error!("file dialog: CoInitializeEx failed: {hr:?}");
    }

    let outcome = if initialized {
        match show(
            kind,
            title.as_deref(),
            default_path.as_deref(),
            &filters,
            owner,
        ) {
            Ok(paths) => paths,
            Err(e) => {
                tracing::error!("file dialog: failed: {e:?}");
                None
            }
        }
    } else {
        None
    };

    if initialized {
        unsafe { CoUninitialize() };
    }
    on_done(outcome);
}

/// Spawns the dialog thread. `true` once the thread owns the request and will
/// invoke `on_done`; `false` leaves `on_done` unrun for the caller to resolve.
pub(crate) fn open(req: FileDialogRequest) -> bool {
    let owner_raw = crate::platform::win_hwnd().map_or(0, |h| h.0 as usize);
    match std::thread::Builder::new()
        .name("jfn-file-dialog".to_string())
        .spawn(move || run(req, owner_raw))
    {
        Ok(_) => true,
        Err(e) => {
            tracing::error!("file dialog: thread spawn failed: {e:?}");
            false
        }
    }
}

#[cfg(windows)]
#[cfg(test)]
mod tests {
    use super::*;

    fn filter(description: &str, extensions: &[&str]) -> FileDialogFilter {
        FileDialogFilter {
            description: description.to_string(),
            extensions: extensions.iter().map(|e| (*e).to_string()).collect(),
        }
    }

    #[test]
    fn spec_joins_extensions_and_labels_the_entry() {
        let (name, spec) = filter_spec(&filter("Image files", &["jpg", "jpeg"])).unwrap();
        assert_eq!(spec, "*.jpg;*.jpeg");
        assert_eq!(name, "Image files (*.jpg;*.jpeg)");
    }

    #[test]
    fn spec_without_description_is_the_globs() {
        let (name, spec) = filter_spec(&filter("  ", &["png"])).unwrap();
        assert_eq!(spec, "*.png");
        assert_eq!(name, "*.png");
    }

    #[test]
    fn filter_without_extensions_is_dropped() {
        assert!(filter_spec(&filter("Nothing", &[])).is_none());
        assert!(filter_spec(&filter("Nothing", &["", ""])).is_none());
    }

    #[test]
    fn all_files_is_always_last() {
        let specs = filter_specs(&[filter("Image files", &["png"])]);
        assert_eq!(specs.len(), 2);
        assert_eq!(specs[0].1, "*.png");
        assert_eq!(specs[1], ("All files (*.*)".to_string(), "*.*".to_string()));
    }

    #[test]
    fn no_filters_still_offers_all_files() {
        let specs = filter_specs(&[]);
        assert_eq!(specs.len(), 1);
        assert_eq!(specs[0].1, "*.*");
    }

    #[test]
    fn unusable_filters_are_skipped_but_all_files_remains() {
        let specs = filter_specs(&[filter("Nothing", &[]), filter("Text", &["txt"])]);
        assert_eq!(specs.len(), 2);
        assert_eq!(specs[0].0, "Text (*.txt)");
        assert_eq!(specs[1].1, "*.*");
    }

    #[test]
    fn options_track_the_kind() {
        assert!(options_for(FileDialogKind::OpenFile).contains(FOS_FORCEFILESYSTEM));
        assert!(options_for(FileDialogKind::OpenFile).contains(FOS_PATHMUSTEXIST));
        assert!(options_for(FileDialogKind::OpenFiles).contains(FOS_ALLOWMULTISELECT));
        assert!(!options_for(FileDialogKind::OpenFile).contains(FOS_ALLOWMULTISELECT));
        assert!(options_for(FileDialogKind::OpenFolder).contains(FOS_PICKFOLDERS));
        assert!(options_for(FileDialogKind::SaveFile).contains(FOS_OVERWRITEPROMPT));
    }

    #[test]
    fn seed_splits_a_file_path_into_folder_and_name() {
        let (folder, name) = seed_from(Path::new("C:/tmp/poster.png"));
        assert_eq!(folder.as_deref(), Some(Path::new("C:/tmp")));
        assert_eq!(name.as_deref(), Some("poster.png"));
    }

    #[test]
    fn seed_of_a_bare_name_has_no_folder() {
        let (folder, name) = seed_from(Path::new("poster.png"));
        assert!(folder.is_none());
        assert_eq!(name.as_deref(), Some("poster.png"));
    }
}
