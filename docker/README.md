# Réplica Docker — SmartSense Monitoring

Réplica local y **neutra** (sin marca de empresa) del dashboard, pensada para:

- Cargar de forma masiva los exportes CSV de la planta (botón **Cargar CSV**).
- Explorar el histórico por sensor y generar las figuras del artículo/tesis.
- Servir de demo genérica reutilizable para cualquier empresa (nombre configurable en `.env`).

Está programado en **Python (Flask)** con almacenamiento **SQLite**; la página es HTML + JavaScript plano.

## Uso

```bash
docker compose up --build
```

Abrir <http://localhost:8500>. Los datos quedan en `./data/lecturas.db` (SQLite, persistente entre reinicios).

> El puerto por defecto es **8500** porque la franja 8080–8161 la ocupan los contenedores de securapp en esta máquina (por eso al abrir el 8080 aparecía la app de RAMO).

El `.env` es **opcional**: si existe, Compose lo lee solo (copiar de `.env.example` para cambiar el nombre visible o el puerto).

## Formato del CSV

Encabezado flexible (se aceptan `,` o `;` y coma decimal de Excel). Columnas reconocidas:

| Dato | Encabezados aceptados | Obligatorio |
|---|---|---|
| Sensor | `sensor`, `mac`, `nodo`, `nombre`, `id` | Sí |
| Fecha y hora | `timestamp`, `ts`, `fecha_hora`, `datetime` — o `fecha` + `hora` en columnas separadas | Sí |
| Temperatura (°C) | `temperatura`, `temp`, `temp_c`, `temperature` | Sí |
| Batería (%) | `bateria`, `battery`, `bat` | No |

Formato canónico recomendado:

```csv
sensor,timestamp,temperatura_c,bateria_pct
Nodo-1,2026-01-15 10:00:00,25.4,87
```

Las filas duplicadas (mismo sensor + misma marca de tiempo) se ignoran, así que se puede recargar el mismo archivo sin duplicar datos.

## Validaciones incluidas

- Archivo: solo `.csv`/`.txt`, no vacío, máximo 64 MB.
- Encabezado irreconocible → mensaje que dice exactamente qué columna falta y qué nombres se aceptan.
- Por fila: fecha/hora inválida, temperatura no numérica o fuera del rango físico de una PT100 (−200 a 850 °C) → la fila se rechaza y se reporta con número de línea y motivo (primeras 10).
- Batería fuera de 0–100 % → se descarta la batería pero **se conserva** la lectura de temperatura.
- Fechas del histórico: formato AAAA-MM-DD; si el rango viene invertido se corrige solo.
- Todos los errores del servidor responden JSON con mensaje claro (nunca una página HTML de error).

## Verificación rápida

```bash
python app.py selftest
```
