# Esquemáticos KiCad — SmartSense

Los esquemáticos **ya están generados y validados** (ERC de KiCad 10: 0 errores, 0 advertencias)
en `hardware/kicad/`:

| Archivo | Contenido |
|---|---|
| `nodo_sensor.kicad_sch` | Nodo: XIAO ESP32-C6 + MAX31865/PT100 + E220-433 + batería con divisor |
| `gateway.kicad_sch` | Gateway: placa ESP32-C6 Touch LCD 1.47 + E220-433 + salida de alarma |
| `smartsense.kicad_sym` | Biblioteca de símbolos propia (editable) |
| `nodo_sensor.pdf` / `gateway.pdf` | Exportes listos para usar como figuras del artículo |
| `svg/*.svg` | Los mismos exportes en vectorial |
| `gen_esquematicos.py` | Generador: produce todo lo anterior desde `CONEXIONES.md` |

Estilo: conexión por etiquetas de red (dos pines con la misma etiqueta están conectados),
que es como se documentan esquemáticos de módulos comerciales interconectados.

## Editar

Abrir el `.kicad_sch` con KiCad (instalado en `%LOCALAPPDATA%\Programs\KiCad\10.0`) y retocar
lo que se quiera — son archivos normales de KiCad. Si el cambio es de conexiones, mejor editar
`gen_esquematicos.py` y regenerar, para que `CONEXIONES.md`, el script y el dibujo no se
desincronicen:

```bash
cd hardware/kicad
python gen_esquematicos.py
kicad-cli sch erc nodo_sensor.kicad_sch     # debe dar 0 errores
kicad-cli sch export pdf -o nodo_sensor.pdf nodo_sensor.kicad_sch
```

(`kicad-cli` está en `%LOCALAPPDATA%\Programs\KiCad\10.0\bin\`.)

## Qué se publica en Zenodo (design files del artículo)

- Carpeta `hardware/kicad/` completa (proyecto editable — requisito OSHWA/HardwareX).
- PDF/SVG del esquemático.
- `BOM.csv` (esta carpeta).
- STL + fuente CAD de la carcasa PLA (agregar cuando estén).
- Firmware (los tres sketches de la versión LoRa, ya sin credenciales).
