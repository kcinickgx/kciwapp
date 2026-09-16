# Reabre la demo (demo-run\) con el exe recien compilado: demo.ps1 [--chat JID]
Get-Process kciwapp2 -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*demo-run*' } | Stop-Process -Force
Start-Sleep -Milliseconds 800
$raiz = Split-Path $PSScriptRoot -Parent
Copy-Item "$raiz\build\kciwapp2.exe" "$raiz\demo-run\" -Force
Start-Process -FilePath "$raiz\demo-run\kciwapp2.exe" -WorkingDirectory "$raiz\demo-run" -ArgumentList (@('--demo') + $args)
