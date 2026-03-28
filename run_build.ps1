
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat'
$buildDir = 'C:\Users\jreng\Documents\Poems\dev\whatdbg\Builds\Ninja'
$logFile = 'C:\Users\jreng\Documents\Poems\dev\whatdbg\build_out.log'
$tmpBat = 'C:\Users\jreng\AppData\Local\Temp\whatdbg_build.bat'

$lines = "@echo off`r`ncall `"$vcvars`" x64`r`ncmake --build `"$buildDir`" --target whatdbg`r`necho BUILD_EXIT=%ERRORLEVEL%`r`n"
[System.IO.File]::WriteAllText($tmpBat, $lines, [System.Text.Encoding]::ASCII)

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = 'cmd.exe'
$psi.Arguments = "/c `"$tmpBat`""
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true

$p = [System.Diagnostics.Process]::Start($psi)
$stdout = $p.StandardOutput.ReadToEnd()
$stderr = $p.StandardError.ReadToEnd()
$p.WaitForExit()

$output = $stdout + $stderr
[System.IO.File]::WriteAllText($logFile, $output, [System.Text.Encoding]::UTF8)
Write-Host "Exit: $($p.ExitCode)"
Write-Host "DONE"
