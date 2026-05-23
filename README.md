# Практика 1 — Tray UI (Win32 + CMake)

Графическое приложение: иконка в системном трее, главное окно, один экземпляр, сборка в CI.

Репозиторий: https://github.com/pagadone1/praktikiZioVpo  
Ветка для сдачи: `assignment/ziovptrayui` → Pull Request в `main`.

Чеклист требований: [docs/PRACTICE1-CHECKLIST.md](docs/PRACTICE1-CHECKLIST.md)

## Сборка (Windows, Visual Studio 2022)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Исполняемый файл: `build\Release\ziovptrayui.exe`

Либо: Visual Studio → **Open Folder** → выбрать эту папку → CMake preset x64-Debug/Release.

## Запуск

| Команда | Поведение |
|---------|-----------|
| `ziovptrayui.exe` | Трей + главное окно |
| `ziovptrayui.exe --hidden` | Только трей (окно не показывается) |

Также: `/hidden`, `--no-window`, `/no-window`.

## CI

Workflow: `.github/workflows/windows-build.yml` (push / pull_request).  
Артефакт: **ziovptrayui-windows-x64** → `ziovptrayui.exe`.

## Структура

| Файл | Назначение |
|------|------------|
| `src/main.cpp` | Окно, трей, меню, мьютекс, message loop |
| `resources/app.rc`, `tray.ico` | Иконка в трее |
| `CMakeLists.txt` | Сборка exe |
| `.github/workflows/windows-build.yml` | GitHub Actions |
