# Licencias de SmartSense Monitoring

Este proyecto es open source. Cada parte tiene su licencia (como exige HardwareX):

| Parte del proyecto | Archivos | Licencia |
|---|---|---|
| **Hardware** (esquemáticos KiCad, cableado, carcasa PLA / CAD / STL) | `hardware/`, `figuras/esquematico-*`, CAD de la carcasa | **CERN-OHL-S v2** — `CERN-OHL-S-v2.txt` |
| **Firmware y software** (sketches Arduino, réplica Docker, scripts de análisis) | `sensores espnow lora/`, `docker/`, `analisis/` | **MIT** — `MIT.txt` |
| **Documentación** (manuscritos, figuras, tablas, README y PDF) | `SmartSense_IEEE.tex`, `hardwarex/`, `SmartSense_Trabajo_Grado.tex`, `tesis/`, `*.md`; los PDF de trabajo `SmartSense_Trabajo_Grado.pdf` y `hardwarex/SmartSense_HardwareX.pdf`; y los PDF de entrega versionados `PDF_Para_Compartir/SmartSense_Trabajo_Grado.pdf` y `PDF_Para_Compartir/SmartSense_HardwareX.pdf` | **CC-BY 4.0** — `CC-BY-4.0.txt` |

Las copias históricas conservadas localmente en `PDF_Para_Compartir/Articulo_SmartSense_HardwareX.pdf` y `PDF_Para_Compartir/Tesis_SmartSense_Monitoring.pdf` también son documentación bajo CC-BY 4.0, pero no forman parte de la entrega versionada. Los dos PDF de entrega vigentes sí son `PDF_Para_Compartir/SmartSense_Trabajo_Grado.pdf` y `PDF_Para_Compartir/SmartSense_HardwareX.pdf`. Esta asignación no modifica las licencias de los materiales de terceros citados o incorporados bajo sus propios términos.

El mapa es coherente con las declaraciones del artículo HardwareX: CERN-OHL-S v2 para hardware, MIT para firmware/software/análisis y CC-BY 4.0 para documentación. La tesis y sus PDF aplican esta misma licencia de documentación.

## Depósito en Zenodo

Versión `v1.0-tesis` archivada el 22 de septiembre de 2026: DOI de concepto
[10.5281/zenodo.22903028](https://doi.org/10.5281/zenodo.22903028), que resuelve a la
versión más reciente (esta versión: 10.5281/zenodo.22903029). El depósito incluye los
esquemáticos KiCad editables, la BOM y las conexiones, el firmware, la réplica Docker,
los scripts de análisis, los STL/3MF de la carcasa, las fuentes de la tesis y del
artículo y estos archivos de licencia. Cada release nuevo en GitHub genera una versión
nueva bajo el mismo DOI de concepto.

⚠️ `secrets.h` está fuera del repositorio y el árbol versionado no contiene credenciales,
pero la clave WiFi antigua sigue en el historial de git y debe rotarse; verificar antes
de cada release.
