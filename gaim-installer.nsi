; Installer script for win32 Gaim
; Generated NSIS script file (generated by makensitemplate.phtml 0.21)
; Herman on Sep 11 02 @ 21:52

; NOTE: this .NSI script is designed for NSIS v1.8+

Name "Gaim 0.60 alpha 3 (Win32)"
OutFile "gaim-0.60-alpha3.exe"
Icon .\pixmaps\gaim-install.ico
UninstallIcon .\pixmaps\gaim-install.ico

; Some default compiler settings (uncomment and change at will):
; SetCompress auto ; (can be off or force)
; SetDatablockOptimize on ; (can be off)
; CRCCheck on ; (can be off)
; AutoCloseWindow false ; (can be true for the window go away automatically at end)
; ShowInstDetails hide ; (can be show to have them shown, or nevershow to disable)
; SetDateSave off ; (can be on to have files restored to their orginal date)

InstallDir "$PROGRAMFILES\Gaim"
InstallDirRegKey HKEY_LOCAL_MACHINE "SOFTWARE\Gaim" ""
DirShow show ; (make this hide to not let the user change it)
DirText "Select the directory to install Gaim in:"

Section "" ; (default section)
  ; Check if previous intallation exists
  ReadRegStr $R0 HKEY_LOCAL_MACHINE "SOFTWARE\gaim" ""
  StrCmp $R0 "" cont_install
    ReadRegStr $R1 HKEY_LOCAL_MACHINE "SOFTWARE\gaim" "Version"
    StrCmp $R1 "" no_version
      ; Gaim found, so exit Intallation
      MessageBox MB_OK "Gaim v$R1 has already been installed. If you wish to install a new version, uninstall first." IDOK
      Quit
      no_version: 
      MessageBox MB_OK "Gaim has already been installed on your machine. If you wish to install a new version, uninstall first." IDOK
      Quit
  cont_install:

  ; Install Aspell
  SetOutPath "$INSTDIR"
  File ..\win32-dev\aspell-15\bin\aspell-0.50.2.exe
  ExecWait "$INSTDIR\aspell-0.50.2.exe"

  SetOutPath "$INSTDIR"
  ; Gaim files
  File /r .\win32-install-dir\*.*

  ; Gaim Registry Settings
  WriteRegStr HKEY_LOCAL_MACHINE "SOFTWARE\Gaim" "" "$INSTDIR"
  WriteRegStr HKEY_LOCAL_MACHINE "SOFTWARE\Gaim" "Version" "0.60a3"
  WriteRegStr HKEY_LOCAL_MACHINE "Software\Microsoft\Windows\CurrentVersion\Uninstall\Gaim" "DisplayName" "Gaim (remove only)"
  WriteRegStr HKEY_LOCAL_MACHINE "Software\Microsoft\Windows\CurrentVersion\Uninstall\Gaim" "UninstallString" '"$INSTDIR\gaim-uninst.exe"'
  ; Set App path to include aspell dir
  WriteRegStr HKEY_LOCAL_MACHINE "Software\Microsoft\Windows\CurrentVersion\App Paths\gaim.exe" "" "$INSTDIR\gaim.exe"
  WriteRegStr HKEY_LOCAL_MACHINE "Software\Microsoft\Windows\CurrentVersion\App Paths\gaim.exe" "Path" "$PROGRAMFILES\aspell"
  ; Increase refrence count for aspell dlls
  Push "C:\Program Files\aspell\aspell-15.dll"
  Call AddSharedDLL
  Push "C:\Program Files\aspell\aspell-common-0-50-2.dll"
  Call AddSharedDLL
  Push "C:\Program Files\aspell\pspell-15.dll"
  Call AddSharedDLL

  ; Set Start Menu icons
  SetOutPath "$SMPROGRAMS\Gaim"
  CreateShortCut "$SMPROGRAMS\Gaim\Gaim.lnk" \
                 "$INSTDIR\gaim.exe"
  CreateShortCut "$SMPROGRAMS\Gaim\Unistall.lnk" \
                 "$INSTDIR\gaim-uninst.exe"

  ; write out uninstaller
  WriteUninstaller "$INSTDIR\gaim-uninst.exe"

  ; cleanup aspell installer file
  Delete "$INSTDIR\aspell-0.50.2.exe"
SectionEnd ; end of default section

; begin uninstall settings/section
UninstallText "This will uninstall Gaim from your system"

Section Uninstall
  ; Delete Gaim Dir
  RMDir /r "$INSTDIR"
  RMDir /r "$SMPROGRAMS\Gaim"

  ; Delete Gaim Registry Settings
  DeleteRegKey HKEY_LOCAL_MACHINE "SOFTWARE\Gaim"
  DeleteRegKey HKEY_LOCAL_MACHINE "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Gaim"
  DeleteRegKey HKEY_LOCAL_MACHINE "SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\gaim.exe"

  ; Decrease refrence count for Aspell dlls
  Push "C:\Program Files\aspell\aspell-15.dll"
  Call un.RemoveSharedDLL
  Push "C:\Program Files\aspell\aspell-common-0-50-2.dll"
  Call un.RemoveSharedDLL
  Push "C:\Program Files\aspell\pspell-15.dll"
  Call un.RemoveSharedDLL

  ; Delete aspell dir if its empty
  RMDir "C:\Program Files\aspell"
SectionEnd ; end of uninstall section

;;;
;;; FUNCTIONS
;;;

; AddSharedDLL
;
; Increments a shared DLLs reference count.
; Use by passing one item on the stack (the full path of the DLL).
;
; Usage: 
;   Push $SYSDIR\myDll.dll
;   Call AddSharedDLL
;

Function AddSharedDLL
  Exch $R1
  Push $R0
  ReadRegDword $R0 HKLM Software\Microsoft\Windows\CurrentVersion\SharedDLLs $R1
  IntOp $R0 $R0 + 1
  WriteRegDWORD HKLM Software\Microsoft\Windows\CurrentVersion\SharedDLLs $R1 $R0
  Pop $R0
  Pop $R1
FunctionEnd

; un.RemoveSharedDLL
;
; Decrements a shared DLLs reference count, and removes if necessary.
; Use by passing one item on the stack (the full path of the DLL).
; Note: for use in the main installer (not the uninstaller), rename the
; function to RemoveSharedDLL.
; 
; Usage:
;   Push $SYSDIR\myDll.dll
;   Call un.RemoveShareDLL
;

Function un.RemoveSharedDLL
  Exch $R1
  Push $R0
  ReadRegDword $R0 HKLM Software\Microsoft\Windows\CurrentVersion\SharedDLLs $R1
  StrCmp $R0 "" remove
    IntOp $R0 $R0 - 1
    IntCmp $R0 0 rk rk uk
    rk:
      DeleteRegValue HKLM Software\Microsoft\Windows\CurrentVersion\SharedDLLs $R1
    goto Remove
    uk:
      WriteRegDWORD HKLM Software\Microsoft\Windows\CurrentVersion\SharedDLLs $R1 $R0
    Goto noremove
  remove:
    Delete /REBOOTOK $R1
  noremove:
  Pop $R0
  Pop $R1
FunctionEnd

; eof

