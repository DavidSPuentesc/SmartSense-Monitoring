# SmartSense Monitoring

Proyecto académico de monitoreo de temperatura superficial industrial con
PT100 de tres hilos, MAX31865, ESP32-C6, comunicación LoRa y consulta histórica
en un dashboard local. Apoya monitoreo de condición; no implementa ni valida
mantenimiento predictivo.

## Contenido del repositorio

- [SmartSense_Trabajo_Grado.tex](SmartSense_Trabajo_Grado.tex): tesis monográfica de una columna, clase `report`, fuera del formato IEEE.
- `tesis/`: preliminares, diez capítulos, anexos, bibliografía autor-fecha y registro compartido `datos-evidencia.tex`.
- [hardwarex/SmartSense_HardwareX.tex](hardwarex/SmartSense_HardwareX.tex): artículo HardwareX.
- `SmartSense_IEEE.tex`: fuente histórica conservada.
- `figuras/`: imágenes y fotografías usadas por ambos documentos.
- `figuras_anteproyecto/`: diagramas del anteproyecto.
- `Entrega_SmartSense/`: entregas anteriores.
- `compilar_trabajo_grado.ps1` y `hardwarex/compilar_hardwarex.ps1`: compilación de los documentos actuales en Windows.
- `compilar_ieee.ps1`: compilación del documento histórico.
- `COMPILAR_LATEX.md`: instrucciones de compilación.

La carpeta `.tools` no se sube porque contiene un ejecutable local. Cada
integrante puede instalar Tectonic o una distribución de LaTeX en su equipo.

## PDF para compartir

| Documento | Entregable actual |
|---|---|
| Trabajo de grado | [SmartSense_Trabajo_Grado.pdf](PDF_Para_Compartir/SmartSense_Trabajo_Grado.pdf) |
| Artículo HardwareX | [SmartSense_HardwareX.pdf](PDF_Para_Compartir/SmartSense_HardwareX.pdf) |

Los archivos previos `PDF_Para_Compartir/Articulo_SmartSense_HardwareX.pdf` y
`PDF_Para_Compartir/Tesis_SmartSense_Monitoring.pdf`, cuando estén presentes,
pertenecen al usuario y deben conservarse sin sobrescribir. Los nuevos
entregables usan los nombres de la tabla. Las fuentes, los PDF compilados
actuales y las nuevas copias de entrega quedan versionados en esta rama.

## Compilación

En Windows:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\compilar_trabajo_grado.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\hardwarex\compilar_hardwarex.ps1
```

Los resultados son `SmartSense_Trabajo_Grado.pdf` en la raíz y
`hardwarex/SmartSense_HardwareX.pdf`. Los scripts no copian automáticamente a
la carpeta de entrega. Véase [COMPILAR_LATEX.md](COMPILAR_LATEX.md) para la
selección de compilador, limpieza limitada de auxiliares, verificación y copia.

## Evidencia y reproducibilidad

Los tres archivos de nodos suman 51.338 muestras: A/C abarcan 8 de mayo a 9 de
junio de 2026 (32 días); B abarca 10 de mayo a 20 de julio. No son 32 días
completos comunes. Disponibilidad durante actividad inferida y PDR estimado
usan filtros distintos. La autonomía se extrapola de cinco días; 688 m es
una cota inferior del recorrido. Los 0,4 °C comunicados no se reconstruyen
con las cinco parejas disponibles, cuyo máximo es 0,1 °C.

Hay firmware, hardware, carcasa STL/3MF, BOM, figuras y scripts. Faltan CSV
primarios, configuración final del piloto e identificación completa de
la PT100 para replicar toda la evaluación. Los manuscritos citan el
[snapshot público de artefactos fuente 5641bcd](https://github.com/DavidSPuentesc/SmartSense-Monitoring/tree/5641bcd89d9026c39ccc62ce8c738ba5ee37781b),
consultado el 15 de septiembre de 2026; no contiene la monografía, sus módulos,
los dos scripts nuevos ni las correcciones locales posteriores del artículo.
Esta rama debe publicarse como versión identificada para cerrar esa parte
de la reproducibilidad; no se declara un DOI existente.

Las licencias están descritas en [LICENSES/README.md](LICENSES/README.md):
CERN-OHL-S v2 para hardware (incluida la BOM), MIT para firmware/software/análisis
y CC-BY 4.0 para documentación. La réplica local está en `docker/` y el firmware
LoRa en `sensores espnow lora/version con lora y pagina local/`.

Para colaborar, repartir capítulos o figuras en ramas y revisar los cambios
antes de integrarlos. No presentar ensayos nuevos sin sus registros primarios.
