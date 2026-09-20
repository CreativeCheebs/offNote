' Runs launch.bat with its console window hidden. QuickNote.lnk points here
' instead of directly at launch.bat so double-clicking the shortcut never
' flashes a terminal window.
Set fso = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")
scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
shell.Run """" & scriptDir & "\launch.bat""", 0, False
