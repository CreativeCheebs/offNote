namespace QuickNote;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();

        var noteForm = new NoteForm();
        // Accessing Handle forces the native window (and its hotkey registration)
        // to exist even though the form is never shown here. Application.Run(form)
        // would auto-Show() it on startup, which we don't want for a tray popup.
        _ = noteForm.Handle;

        Application.Run(new TrayApplicationContext(noteForm));
    }
}

internal sealed class TrayApplicationContext : ApplicationContext
{
    public TrayApplicationContext(NoteForm noteForm) : base()
    {
        noteForm.FormClosed += (_, _) => ExitThread();
    }
}
