# SmartSense Monitoring — Tabla de conexiones (verificada contra firmware)

Fuente de verdad: `Nodo_Lora.ino`, `maestro_Lora_pantalla.ino`, `Display_ST7789.h`, `SD_Card.h`.
Estas tablas son el insumo directo para dibujar el esquemático en KiCad (ver `GUIA_KICAD.md`).

## 1. Nodo sensor (XIAO ESP32-C6)

### 1.1 Módulo LoRa Ebyte E220-433 (UART)

| E220 | XIAO ESP32-C6 | GPIO | Nota |
|---|---|---|---|
| TXD | D7 | GPIO17 | UART RX del nodo (`PIN_E220_RX`) |
| RXD | D8 | GPIO19 | UART TX del nodo (`PIN_E220_TX`), 9600 bps 8N1 |
| M0 | D10 | GPIO18 | Selección de modo |
| M1 | D9 | GPIO20 | Selección de modo |
| AUX | — | — | Sin conectar (no usado por firmware) |
| VCC | 3V3 | — | |
| GND | GND | — | |

### 1.2 Acondicionador MAX31865 (módulo tipo Adafruit, SPI por software)

| MAX31865 | XIAO ESP32-C6 | GPIO | Nota |
|---|---|---|---|
| CLK | D6 | GPIO16 | `PIN_SCK` |
| SDO | D5 | GPIO23 | `PIN_MISO` |
| SDI | D4 | GPIO22 | `PIN_MOSI` |
| CS | D3 | GPIO21 | `PIN_CS` |
| VIN | D2 | GPIO2 | `PIN_SENSOR_PWR`: el módulo se alimenta desde un GPIO para poder apagarlo en deep sleep |
| GND | GND | — | |
| RDY | — | — | Sin conectar |

Parámetros de firmware: `RREF = 421,1 Ω` (valor calibrado del módulo usado; el nominal típico de estos módulos es 430 Ω), `RNOMINAL = 100 Ω` (PT100).

### 1.3 Sonda PT100 de 3 hilos → bornes del MAX31865

- Puente **2/3-Wire** del módulo: soldado (modo 3 hilos).
- Los dos hilos del mismo extremo de la sonda → bornes **F+** y **RTD+**.
- El hilo restante → borne **RTD−** (unido a **F−** por el puente del módulo).

> Verificar contra el montaje físico real de los nodos; la referencia es la guía oficial del módulo Adafruit MAX31865.

### 1.4 Batería y medición de carga

| Elemento | Conexión |
|---|---|
| Batería LiPo (3,7 V) | BAT+ / BAT− → pads de batería del XIAO (regulador a bordo) |
| Módulo de carga/protección (tipo TP4056) | Entre la celda y los pads BAT del XIAO |
| Divisor resistivo | BAT+ → **R1 = 344,8 kΩ** → nodo A0 (GPIO0, `BAT_ADC_PIN`) → **R2 = 994,3 kΩ** → GND |

Factor del divisor: (R1+R2)/R2 ≈ 1,347 → 4,2 V de celda ≈ 3,12 V en el ADC.
Calibración en firmware: `BAT_CAL_K = 1,000`, `BAT_CAL_B = 0,090` (dejar como constantes ajustables: cada divisor real varía).

## 2. Gateway / visualizador (placa integrada ESP32-C6 Touch LCD 1.47)

La pantalla ST7789, el táctil y la ranura microSD vienen **integrados en la placa** (SPI: SCLK=GPIO1, MOSI=GPIO2, MISO=GPIO3, LCD_CS=GPIO14, DC=GPIO15, SD_CS=GPIO4; táctil I2C: SDA=GPIO18, SCL=GPIO19). En el esquemático se documentan solo las conexiones externas:

| Elemento externo | Placa gateway | GPIO | Nota |
|---|---|---|---|
| E220 TXD | — | GPIO8 | UART RX del gateway (`PIN_E220_RX`) |
| E220 RXD | — | GPIO7 | UART TX del gateway (`PIN_E220_TX`), 9600 bps 8N1 |
| E220 M0 **y** M1 | — | GPIO9 | Ambos pines del E220 unidos al mismo GPIO |
| E220 VCC / GND | 3V3 / GND | — | |
| Salida alarma crítica | — | GPIO16 | `PIN_ALARMA_CRITICA` (LED/buzzer/relé según instalación) |

## 3. Parámetros del enlace LoRa (ambos extremos)

| Parámetro | Valor |
|---|---|
| UART | 9600 bps, 8N1 |
| Tasa aérea | 2,4 kbps |
| Canal | 23 (banda 433 MHz) |
| Modo | Transmisión fija (fixed), paquete 200 bytes |
| Direcciones | Maestro = 1 · Nodos = dirección propia (ej. 13) |
