# GTK desktop experiment

This Windows GTK 3 prototype tests memory use for a quick capture window with a
tray icon and global `Alt+Space` hotkey. It writes each note to the shared
`%APPDATA%\QuickNote\quicknote.db` SQLite outbox before clearing the editor.
`Ctrl+Enter` or `Alt+Enter` saves; Escape hides the window. Start it with
`launch-gtk.bat` from the repository root.

Build with `build.bat` after installing the MSYS2 `mingw-w64-x86_64-gtk3`
package. It also uses the MinGW GCC and SQLite packages. The launcher uses
libraries from `C:\msys64\mingw64\bin`, so this is a local prototype rather
than a redistributable package.

The prototype does not yet route or deliver saved notes. Rows stay pending in
the shared database for a full client to process. Do not run two desktop
clients with the same global hotkey; only one can register `Alt+Space`.
