# Licencias de SmartSense Monitoring

Este proyecto es open source. Cada parte tiene su licencia (como exige HardwareX):

| Parte del proyecto | Archivos | Licencia |
|---|---|---|
| **Hardware** (esquemáticos KiCad, cableado, carcasa PLA / CAD / STL) | `hardware/`, `figuras/esquematico-*`, CAD de la carcasa | **CERN-OHL-S v2** — `CERN-OHL-S-v2.txt` |
| **Firmware y software** (sketches Arduino, réplica Docker, scripts de análisis) | `sensores espnow lora/`, `docker/`, `analisis/` | **MIT** — `MIT.txt` |
| **Documentación** (manuscritos, figuras, tablas, README y PDF) | `SmartSense_IEEE.tex`, `hardwarex/`, `SmartSense_Trabajo_Grado.tex`, `tesis/`, `*.md`; los PDF de trabajo `SmartSense_Trabajo_Grado.pdf` y `hardwarex/SmartSense_HardwareX.pdf`; y los PDF de entrega versionados `PDF_Para_Compartir/SmartSense_Trabajo_Grado.pdf` y `PDF_Para_Compartir/SmartSense_HardwareX.pdf` | **CC-BY 4.0** — `CC-BY-4.0.txt` |

Las copias históricas conservadas localmente en `PDF_Para_Compartir/Articulo_SmartSense_HardwareX.pdf` y `PDF_Para_Compartir/Tesis_SmartSense_Monitoring.pdf` también son documentación bajo CC-BY 4.0, pero no forman parte de la entrega versionada. Los dos PDF de entrega vigentes sí son `PDF_Para_Compartir/SmartSense_Trabajo_Grado.pdf` y `PDF_Para_Compartir/SmartSense_HardwareX.pdf`. Esta asignación no modifica las licencias de los materiales de terceros citados o incorporados bajo sus propios términos.

El mapa es coherente con las declaraciones del artículo HardwareX: CERN-OHL-S v2 para hardware, MIT para firmware/software/análisis y CC-BY 4.0 para documentación. La tesis y sus PDF aplican esta misma licencia de documentación.

## Depósito en Zenodo (pendiente)

Snapshot a subir a Zenodo (para el DOI que pide HardwareX):

- [ ] Esquemáticos KiCad editables (`hardware/kicad/`) + PDF/SVG.
- [ ] `hardware/BOM.csv` y `hardware/CONEXIONES.md`.
- [ ] Réplica Docker (`docker/`) y scripts de análisis (`analisis/`).
- [ ] **CAD editable + STL** de la carcasa (cuando estén).
- [ ] Estos archivos de licencia.

⚠️ Antes de publicar: una credencial histórica expuesta debe rotarse; verificar que ningún archivo del snapshot contenga credenciales. El DOI de Zenodo se anota luego en la tabla de especificaciones del artículo y en `CERN-OHL-S-v2.txt`.
