# Sensores ESP-NOW y LoRa

Repositorio de desarrollo del sistema SmartSense para adquisición de
temperatura, comunicación entre nodos y visualización local.

## Versiones incluidas

- `version con espnow/`: prototipo de comunicación ESP-NOW.
- `version con pagina web en wifi local/`: versión con interfaz web en la red
  Wi-Fi local.
- `version con lora y pagina local/`: versión actual con nodos LoRa, maestro
  con pantalla, dashboard local y módulos de alarma.

La copia `.rar`, los cachés de Python y los ajustes personales de VS Code no
se versionan.

## Trabajo en paralelo

Antes de comenzar una tarea:

```powershell
git switch main
git pull
git switch -c nombre/tarea
```

Al terminar:

```powershell
git add .
git commit -m "Describe el cambio"
git push -u origin nombre/tarea
```

Después se abre un *pull request* hacia `main`. Cada persona debe trabajar en
una rama diferente y evitar editar simultáneamente el mismo sketch.

## Seguridad

El repositorio debe permanecer privado. Algunos prototipos contienen
credenciales locales necesarias para las pruebas de Wi-Fi y actualización OTA.
Antes de publicar el proyecto o compartirlo con terceros, esas credenciales
deben moverse a archivos locales ignorados por Git y cambiarse en los equipos.

