set DEST="D:\JOC\RTCW\rtcw_orig"
copy /Y *.dll %DEST%
if %ERRORLEVEL% NEQ 0 pause
copy /Y WolfSP.exe %DEST%
if %ERRORLEVEL% NEQ 0 pause
copy /Y WolfSP.pdb %DEST%
if %ERRORLEVEL% NEQ 0 pause