# Практика 2 — служба + tray (RPC/ALPC)

Ветка: `assignment/pract2-service-rpc` → PR в `main`.  
Связь с практикой 1: `TrayWin32App.exe` (трей из задания 1, доработан для RPC).

## Обязательные требования

### Служба (`Pract2Service.exe`, `src/service.cpp`)

| № | Требование | Реализация |
|---|------------|------------|
| 1 | Запуск GUI во всех сессиях ≠ 0, от имени пользователя, окно скрыто | `LaunchTrayAppsInExistingSessions`, `LaunchTrayAppInSession` + `--hidden --service-child`, `CreateProcessAsUserW`, `SW_HIDE` |
| 2 | Новые входы пользователей | `SERVICE_CONTROL_SESSIONCHANGE` (logon/unlock/connect) |
| 3 | Не обрабатывать Stop/Shutdown SCM | `ServiceControlHandlerEx`: `return NO_ERROR` без остановки |
| 4 | RPC-сервер, транспорт ALPC, работа пока RPC жив | `ncalrpc`, `RpcServerListen`, цикл до `g_stop_event` |
| 5 | RPC-интерфейс остановки службы | `RpcStopService` в `Pract2Control.idl` |
| 6 | При остановке — завершить все GUI | `TerminateChildren()` |

### Графическое приложение (`TrayWin32App.exe`, `src/main.cpp`)

| № | Требование | Реализация |
|---|------------|------------|
| 1 | Если служба остановлена — запустить, дождаться Running, выйти | `EnsureServiceStartedIfNeeded()` (ручной запуск без `--service-child`) |
| 2 | Родитель — только служба | `--service-child` + `IsParentServiceProcess()` |
| 3 | Файл → Выход — остановить службу | `StopServiceAndExit()` → `RpcStopService` |
| 4 | Выход в меню трея — остановить службу | то же |

## Дополнительные баллы (бонусы)

| № | Требование | Статус |
|---|------------|--------|
| 1 | Подтверждение остановки на отдельном desktop активной сессии | `ConfirmStopOnActiveSession`, `--secure-stop-confirm` |
| 2 | DACL процесса службы | `ApplyRestrictTerminateDacl(GetCurrentProcess())` при старте |
| 3 | DACL дочернего tray-процесса | `ApplyRestrictTerminateDacl` после `CreateProcessAsUserW` |
| 4 | Защита от завершения администраторами | частично (deny terminate для AU; BA/SY сохраняют полный доступ) |

## Доп. задания к практикам (auth / license) — в этой ветке

Реализованы **на стороне службы** через RPC (`rpc/Pract2Control.idl`):

| Доп. задание | RPC-методы |
|--------------|------------|
| Регистрация / авторизация | `RpcRegisterUser`, `RpcLogin`, `RpcLogout`, `RpcGetAuthenticatedUser` |
| Лицензия | `RpcActivateProduct`, `RpcGetLicenseInfo` |
| Антивирус (доступ по лицензии) | `RpcGetAntivirusStatus` |

Демо: пользователь `admin` / `123456`, ключ `PRACT2-DEMO-KEY`.  
Клиентский UI для RPC в tray пока не обязателен для базового задания 2; проверка — любым RPC-клиентом или тестовым вызовом.

## Проверка на Windows (администратор)

1. `GO.cmd` — сборка, установка, старт службы.
2. `check.cmd` — Running, `TrayWin32App.exe` в tasklist, логи в `C:\ProgramData\Pract2Service\`.
3. Иконка в трее, ЛКМ — окно, ПКМ → Выход — диалог, служба останавливается.
4. Двойной клик `TrayWin32App.exe` — процесс сразу завершается (bootstrap).
5. `TrayWin32App.exe --service-child` без службы-родителя — завершение (после исправления проверки родителя).

## CI

`.github/workflows/windows-build.yml` — `Pract2Service.exe`, `TrayWin32App.exe`.
