@echo off
set OutDir=%1

IF "%OutDir%"=="" (
  set OutDir=rftg
)

IF NOT EXIST "Release\rftg.exe" (
  echo You must build the Release configuration before installing.
  exit /b
)

xcopy "Release\rftg.exe" "%OutDir%\" /y
xcopy "..\..\src\COPYING" "%OutDir%\" /y
xcopy "..\..\src\README" "%OutDir%\" /y
xcopy "..\..\src\cards.txt" "%OutDir%\" /y
xcopy "..\..\src\campaign.txt" "%OutDir%\" /y
xcopy "..\..\src\images.data" "%OutDir%\" /y
xcopy "..\..\src\network\*.net" "%OutDir%\network\" /i /y
xcopy "..\..\3rdparty\win\*" "%OutDir%\" /i /s /y

echo Installed to %OutDir%
