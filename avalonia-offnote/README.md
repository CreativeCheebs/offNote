# Avalonia desktop prototype

This is an initial .NET 10 Avalonia capture window. It reuses the existing C#
configuration, routing, connector, and SQLite code from `winforms-quicknote`.
It has a tray icon (New note / Quit) and an `Alt+Space` global hotkey, but is not
yet a full replacement for the WinForms app (no settings or retry UI).

Build with the repository-local SDK from the repository root:

```powershell
.\.dotnet8\dotnet.exe build .\avalonia-offnote\Offnote.Avalonia.csproj
```

The window is frameless with no title bar or Save button: drag it by its
background, press `Ctrl+Enter` to save, and press `Esc` to hide it to the tray
(a non-empty note needs a second `Esc` and is then discarded). A successful
save flashes the box green (amber if delivery is pending), shows "Saved", and
hides the window. Quit from the tray menu. If another app already holds
`Alt+Space`, the window says so and the tray icon still opens it.

Memory settings: software rendering (no ANGLE/DirectComposition surfaces),
non-concurrent workstation GC with `GCConserveMemory`, invariant globalization,
and no tiered PGO. Measured on this machine (Release build, working set / private):
visible and empty about 77 / 21 MB; after typing 360 characters about 88 / 31 MB;
hidden in the tray about 18 / 29 MB (the working set is trimmed on hide, so use
the private figure). Before these changes: about 173 / 109 MB.

The app uses Fluent styling as a baseline. The current WinForms app remains the
default launcher until the Avalonia version has equivalent capture behavior and
its memory use has been measured.

Initial Windows measurement: the Release build's capture window used about
178 MB working set after launch. This is below the Compose desktop measurement
(about 336 MB) but above the WinForms measurement (about 76 MB). These are
single-process snapshots, not a full idle and active benchmark.
