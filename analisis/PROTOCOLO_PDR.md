# Protocolo de prueba de PDR (con el firmware instrumentado)

El firmware ahora permite medir el PDR real (paquetes enviados vs. recibidos),
que es la evidencia que pide el jurado ("muéstreme de dónde salió ese 95%").

## Qué cambió

- **Nodo** (`Nodo_Lora.ino`): cada trama de datos lleva un contador `SEQ` que
  se incrementa en cada envío y sobrevive al deep sleep (`RTC_DATA_ATTR`). El
  primer paquete es `SEQ=1`. Al cortar la alimentación el contador vuelve a
  empezar (el gateway lo detecta y reinicia la ventana de medición).
  Trama nueva: `DATA,ID=13,SEQ=42,TEMP=64.50,BAT=87.0,MAC=...`
- **Gateway** (`maestro_Lora_pantalla.ino`): por cada paquete recibido imprime
  en el **monitor serie** una línea de PDR por nodo:
  `[PDR] mac=58:E6:C5:13:A3:C0 seq=42 enviados=42 recibidos=41 perdidos=1 PDR=97.62%`
  - `enviados = último SEQ - primer SEQ + 1` (lo que el nodo intentó enviar)
  - `recibidos` = paquetes que llegaron al gateway
  - `perdidos = enviados - recibidos`

Compatibilidad: un nodo sin reflashear (sin `SEQ`) sigue funcionando; solo no
genera línea de PDR. No se tocó el almacenamiento, la base ni el dashboard.

## Cómo correr la prueba

1. Reflashear **los 3 nodos** con el `Nodo_Lora.ino` nuevo y el gateway con el
   `maestro_Lora_pantalla.ino` nuevo (Arduino IDE, mismo procedimiento de siempre).
2. Encender el gateway y abrir el **Monitor Serie** (115200 baudios). Dejarlo
   grabando a un archivo (en Arduino IDE, o con PuTTY/`pio device monitor`).
3. Encender los 3 nodos **a la vez** y anotar la hora de inicio.
4. Dejar correr una sesión controlada. Con muestreo de 120 s:
   - 1 hora ≈ 30 paquetes por nodo (90 en total).
   - 2 horas ≈ 60 por nodo (buena base estadística).
   - Para más resolución, bajar el intervalo a 30 s desde el dashboard durante
     la prueba (más paquetes en menos tiempo).
5. Al terminar, la **última línea `[PDR]` de cada nodo** ya trae el resultado
   final: enviados, recibidos, perdidos y PDR. Esa es la evidencia directa.

## Para la tabla de la sustentación

Ejemplo de cómo presentarlo (rellenar con los números reales de la prueba):

| Nodo | Enviados | Recibidos | Perdidos | PDR |
|------|----------|-----------|----------|-----|
| A    | 60       | 58        | 2        | 96.7% |
| B    | 60       | 59        | 1        | 98.3% |
| C    | 60       | 57        | 3        | 95.0% |

- **PDR** = recibidos / enviados. Mide el desempeño del **enlace**.
- **Disponibilidad** = mediciones disponibles para el usuario / esperadas. En
  este sistema es casi igual al PDR salvo cuando el gateway estuvo apagado
  (esas ventanas se excluyen porque no dependen del enlace). Esta es la
  diferencia concreta que puede preguntar el jurado (punto 7 de la revisión).

## Lo que este cambio NO cubre (honestidad para la sustentación)

- **RSSI / SNR**: el E220 solo entrega RSSI si se habilita el byte de RSSI en su
  registro de configuración y se lee ese byte binario tras cada paquete; es un
  cambio mayor y dependiente del hardware. Recomendación: presentarlo como
  trabajo futuro, no como resultado del prototipo actual.
- **Latencia**: sigue sin poder medirse extremo a extremo sin sincronizar el
  reloj del nodo con el del gateway. No presentar una cifra de latencia como
  medición exacta si no hubo sincronización.
- **Exactitud de temperatura, autonomía y recuperación**: requieren sus propias
  pruebas físicas (sonda de referencia, descarga real, interrupción controlada),
  no se obtienen de este contador.
