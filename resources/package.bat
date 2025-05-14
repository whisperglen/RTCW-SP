mkdir .\src\Release\package\.trex
mkdir .\src\Release\package\main
mkdir .\src\Release\package\crashes
copy .\src\Release\cgamex86.dll .\src\Release\package\
copy .\src\Release\qagamex86.dll .\src\Release\package\
copy .\src\Release\uix86.dll .\src\Release\package\
copy .\src\Release\WolfSP.exe .\src\Release\package\
copy .\src\Release\WolfSP.pdb .\src\Release\package\
copy .\src\Release\bridge.conf .\src\Release\package\.trex\
copy .\src\Release\autoexec.cfg .\src\Release\package\main\
copy .\src\Release\wolf_customise.ini .\src\Release\package\
copy .\src\Release\CopyCrashDumpHere.bat .\src\Release\package\crashes
copy .\src\Release\CrashConfig.reg .\src\Release\package\crashes
7z a -tzip wolf_dx9_game_binaries.zip .\src\Release\package\*