# Ручная установка drop-in

Это расширенный/legacy-способ. Новым пользователям рекомендуется
[графический лаунчер-установщик](INSTALL_RU.md).

## Установка

1. Закройте игру и relay-процесс Alice Co-op.
2. Сделайте резервную копию сохранений и существующих `dinput8.dll`,
   `MadnessPatch.ini` и `AliceCoop.ini`.
3. Скачайте `AliceCoop-<версия>-drop-in.zip` и распакуйте его содержимое прямо
   в папку с `AliceMadnessReturns.exe`.
4. Разрешите объединение папок. При обновлении сохраните изменённые INI-файлы.

Обычные пути:

```text
Steam:  <SteamLibrary>\steamapps\common\Alice Madness Returns\Binaries\Win32
EA App: <папка EA>\Alice Madness Returns\Game\Alice2\Binaries\Win32
```

Получится следующая структура:

```text
AliceMadnessReturns.exe
dinput8.dll
MadnessPatch.ini
AliceCoop\AliceCoopLauncher.exe
AliceCoop\AliceCoopServer.exe
AliceCoop\AliceCoop.ini
AliceCoop\images\...
AliceCoop\Advanced\Manual\...
```

Объединённый `dinput8.dll` уже содержит MadnessPatch 3.1.1. Не заменяйте его
другой proxy DLL.

## Запуск через лаунчер из архива

Запустите `AliceCoop\AliceCoopLauncher.exe`, выберите текущую папку игры и
используйте **Host Game** или **Join Game**. Не перемещайте содержимое этой
папки во время сессии.

## Legacy-запуск через BAT/INI

Ручные скрипты находятся в `AliceCoop\Advanced\Manual`:

1. Откройте `AliceCoop-LaunchConfig.bat` и задайте адрес relay, порт и режим
   отображения.
2. На компьютере с relay запустите `AliceCoop-Server.bat`.
3. Для хоста запустите `AliceCoop-Host.bat`, для клиента —
   `AliceCoop-Client.bat`. `AliceCoop-Both.bat` предназначен только для
   локального тестирования.
4. Если Steam перезапускает игру и теряет переменные лаунчера, задайте
   `EnableWithoutLauncher = 1`, `Role`, `ServerAddress` и `ServerPort` в
   `AliceCoop\AliceCoop.ini` каждой установки, затем запустите игру через Steam.

Используйте доверенную LAN/VPN и разрешите `AliceCoopServer.exe` в частном
профиле брандмауэра Windows. Не открывайте relay без аутентификации напрямую в
Интернет.

## Обновление и удаление

Для обновления закройте игру и relay, замените файлы из архива и сохраните свои
`AliceCoop.ini` и `MadnessPatch.ini`. У обоих игроков должна быть одна версия.

Для удаления запустите
`AliceCoop\Advanced\Tools\Uninstall-AliceCoop.bat`. Если мод был установлен
только drag-and-drop и install manifest отсутствует, перед ручным удалением
`dinput8.dll` проверьте резервную копию MadnessPatch.
