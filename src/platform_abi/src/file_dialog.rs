//! Native file-chooser requests handed from CEF's dialog handler down to the
//! platform backend.
//!
//! CEF's own file chooser assumes a windowed (aura-backed) browser, so a
//! windowless client must claim `OnFileDialog` itself and open the OS dialog.
//! [`FileDialogRequest`] is the backend-agnostic shape of that ask; the result
//! comes back through [`FileDialogRequest::on_done`], which the backend
//! invokes exactly once.

use std::path::PathBuf;

/// What the page asked for; maps 1:1 onto `cef_file_dialog_mode_t`.
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub enum FileDialogKind {
    OpenFile,
    OpenFiles,
    OpenFolder,
    SaveFile,
}

impl FileDialogKind {
    /// True when more than one path may come back.
    pub fn multiple(self) -> bool {
        matches!(self, FileDialogKind::OpenFiles)
    }
}

/// One entry of the dialog's type dropdown. `extensions` carry no leading dot
/// (`"jpg"`, not `".jpg"`) and are lowercase.
#[derive(Clone, PartialEq, Eq, Debug, Default)]
pub struct FileDialogFilter {
    pub description: String,
    pub extensions: Vec<String>,
}

/// One file-chooser ask. `on_done` receives the chosen paths, or `None` when
/// the user cancelled or the dialog could not be shown.
pub struct FileDialogRequest {
    pub kind: FileDialogKind,
    pub title: Option<String>,
    /// Seed folder, or seed folder + file name when it names a file.
    pub default_path: Option<PathBuf>,
    pub filters: Vec<FileDialogFilter>,
    pub on_done: Box<dyn FnOnce(Option<Vec<PathBuf>>) + Send>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_open_files_is_multiple() {
        assert!(FileDialogKind::OpenFiles.multiple());
        assert!(!FileDialogKind::OpenFile.multiple());
        assert!(!FileDialogKind::OpenFolder.multiple());
        assert!(!FileDialogKind::SaveFile.multiple());
    }

    #[test]
    fn on_done_carries_the_result() {
        let (tx, rx) = std::sync::mpsc::channel();
        let req = FileDialogRequest {
            kind: FileDialogKind::OpenFile,
            title: None,
            default_path: None,
            filters: vec![FileDialogFilter {
                description: "Image files".to_string(),
                extensions: vec!["png".to_string()],
            }],
            on_done: Box::new(move |paths| {
                let _ = tx.send(paths);
            }),
        };
        assert_eq!(req.filters.len(), 1);
        (req.on_done)(Some(vec![PathBuf::from("a.png")]));
        assert_eq!(rx.recv().ok().flatten(), Some(vec![PathBuf::from("a.png")]));
    }
}
