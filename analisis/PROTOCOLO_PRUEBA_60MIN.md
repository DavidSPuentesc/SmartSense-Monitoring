# Protocolo de prueba instrumentada (60 min)

Firmware actualizado para capturar, por paquete: **PDR (contador SEQ), temperatura,
batería (voltaje real), RSSI y tiempo encendido**. El gateway imprime una línea
`CSV,...` por paquete en el monitor serie.

## ANTES de la prueba (hoy) — verificación obligatoria

Los cambios de firmware NO se pudieron compilar/probar aquí. Tras flashear:

1. Reflashear los **3 nodos** (`Nodo_Lora.ino`) y el **gateway** (`maestro_Lora_pantalla.ino`).
2. Abrir el Monitor Serie del gateway (115200) y confirmar:
   - Aparece `[E220][RSSI] Habilitando byte de RSSI...` y una respuesta del módulo.
   - **Sigue llegando la recepción normal** (líneas `[PDR]` y `CSV,` cuando un nodo transmite).
   - En las líneas `CSV,` el penúltimo campo (`rssi_raw`) es **distinto de -1** (llegó RSSI).
3. Si la recepción se rompió o `rssi_raw` siempre es -1:
   - Poner `#define TRY_RSSI 0` en el gateway y reflashear (se pierde RSSI pero todo lo demás funciona).
   - El comando `AT+RSSI=1` puede variar según la variante del módulo; ese es el punto a revisar.

## Durante la prueba

1. Cargar los 3 nodos al 100 % y anotar el voltaje inicial de cada uno.
2. Encender el gateway con el **Monitor Serie grabando a un archivo** (Arduino IDE: no graba;
   usar PuTTY/`pio device monitor -f log`/CoolTerm para guardar a `.txt`).
3. Encender los 3 nodos a la vez. Anotar **hora de inicio** y la **distancia** nodo–gateway.
4. Dejar correr **60 min** (o bajar el intervalo a 30 s desde el dashboard para más paquetes).
5. Al terminar, guardar el `.txt` del monitor serie.

## Después

```
python analisis/analizar_prueba.py  <log_del_monitor_serie.txt>
```
Saca por nodo: PDR/pérdida, temperatura (min/máx/prom), batería (inicio/fin) y RSSI (min/máx/prom).

## Qué queda fuera (limitaciones honestas)

- **SNR**: el E220 no lo expone (necesitaría un SX1262 pelado).
- **Latencia extremo a extremo**: requiere sincronizar el reloj nodo↔gateway.
- **MAE ≤1 °C**: requiere una termocupla/RTD calibrada (el Fluke y la FLIR son IR).

## Formato de la línea CSV (para futuras pruebas)

```
CSV,epoch,mac,id,seq,temp_c,vbat,bat_pct,rssi_dbm,rssi_raw,up_ms
```
