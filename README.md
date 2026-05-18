# ZiovpoTrayUi

Графическое Win32-приложение с иконкой в системном трее.

## Сборка

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Исполняемый файл: `build/Release/ZiovpoTrayUi.exe`

## CI

Сборка на GitHub Actions: `.github/workflows/windows-build.yml`  
Артефакт `ZiovpoTrayUi-windows-x64` доступен после успешного workflow.

## Репозиторий

https://github.com/pagadone1/praktikiZioVpo