# Licencias de SmartSense Monitoring

Este proyecto es open source. Cada parte tiene su licencia (como exige HardwareX):

| Parte del proyecto | Archivos | Licencia |
|---|---|---|
| **Hardware** (esquemáticos KiCad, cableado, carcasa PLA / CAD / STL) | `hardware/`, `figuras/esquematico-*`, CAD de la carcasa | **CERN-OHL-S v2** — `CERN-OHL-S-v2.txt` |
| **Firmware y software** (sketches Arduino, réplica Docker, scripts de análisis) | `sensores espnow lora/`, `docker/`, `analisis/` | **MIT** — `MIT.txt` |
| **Documentación** (texto del artículo, figuras, tablas, READMEs) | `SmartSense_IEEE.tex`, `hardwarex/`, `*.md` | **CC-BY 4.0** — `CC-BY-4.0.txt` |

## Depósito en Zenodo (pendiente)

Snapshot a subir a Zenodo (para el DOI que pide HardwareX):

- [ ] Esquemáticos KiCad editables (`hardware/kicad/`) + PDF/SVG.
- [ ] Los tres sketches de firmware de la versión LoRa **sin `secrets.h`** (las claves quedan fuera).
- [ ] `hardware/BOM.csv` y `hardware/CONEXIONES.md`.
- [ ] Réplica Docker (`docker/`) y scripts de análisis (`analisis/`).
- [ ] **CAD editable + STL** de la carcasa (cuando estén).
- [ ] Estos archivos de licencia.

⚠️ Antes de publicar: rotar la clave WiFi `elyisus` (sigue en el historial de git) y verificar que ningún archivo del snapshot contenga credenciales. El DOI de Zenodo se anota luego en la tabla de especificaciones del artículo y en `CERN-OHL-S-v2.txt`.
