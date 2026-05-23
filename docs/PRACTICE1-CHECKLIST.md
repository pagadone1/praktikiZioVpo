# Практика 1 — Tray UI (проверка требований)

## Соответствие заданию

| № | Требование | Где в коде / как проверить |
|---|------------|----------------------------|
| 1 | Иконка в трее при запуске | `AddTrayIcon()` в `wWinMain` после старта |
| 2 | ЛКМ по иконке → главное окно | `HandleTrayNotification` → `ShowMainWindow()` |
| 3 | ПКМ → контекстное меню | `ShowTrayMenu()` |
| 4 | Пункт «Открыть» | `kMenuOpen` → `ShowMainWindow()` |
| 5 | Пункт «Выход» в меню трея | `kMenuExit` → `ExitApplication()` |
| 6 | Пересоздание панели задач | `RegisterWindowMessage(L"TaskbarCreated")` → `RecreateTrayIcon()` |
| 7 | Запуск без окна | `ziovptrayui.exe --hidden` (или `/hidden`, `--no-window`) |
| 8 | Закрытие окна → фон | `WM_CLOSE` → `SW_HIDE` (не `DestroyWindow`) |
| 9 | Меню «Файл» → «Выход» | `CreateMainMenu()` → `kMenuFileExit` |
| 10 | Один экземпляр | `CreateMutexW` + `ERROR_ALREADY_EXISTS` **до** `AddTrayIcon()` |
| 11 | CI CMake/MSBuild | `.github/workflows/windows-build.yml` |
| 12 | Артефакт exe | job upload `ziovptrayui.exe` |

**Бонус:** Win32 API + **CMake** (не WinUI).

## Ручная проверка (Windows)

1. Собрать: Visual Studio → Open Folder → CMake, или `cmake --build build --config Release`.
2. Запустить `build\Release\ziovptrayui.exe` — иконка в трее, окно видно.
3. Закрыть окно крестиком — процесс в диспетчере задач, иконка в трее есть.
4. ЛКМ по иконке — окно снова.
5. ПКМ → «Открыть» / «Выход».
6. «Файл» → «Выход» в окне — процесс завершился.
7. Второй запуск exe — сразу завершается, в трее одна иконка.
8. `ziovptrayui.exe --hidden` — только трей, окна нет.
9. Перезапуск проводника (`taskkill /f /im explorer.exe` && `start explorer`) — иконка появляется снова.

## Сдача

Ветка: `assignment/ziovptrayui` → PR в `main` репозитория `pagadone1/praktikiZioVpo`.  
В PR только файлы этой практики (папка tray-ui или отдельный репо — как у преподавателя).

После push: GitHub Actions → артефакт **ziovptrayui-windows-x64**.
