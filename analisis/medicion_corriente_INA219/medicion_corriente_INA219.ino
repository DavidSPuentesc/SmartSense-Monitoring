/*
 * Medición de corriente del nodo SmartSense con INA219.
 *
 * Coloca el INA219 en serie con la batería (Vin+ a batería+, Vin- a nodo+),
 * y léelo por I2C. El sketch detecta el pulso activo del nodo (cuando la
 * corriente sube por encima de un umbral), lo promedia y estima la autonomía.
 *
 * Objetivo: obtener la CORRIENTE ACTIVA promedio (los ~2,279 s de arranque +
 * lectura + transmisión LoRa). El consumo en sueño profundo (µA) queda por
 * debajo de la resolución del INA219, así que se toma del datasheet del
 * ESP32-C6 (constante I_SLEEP_UA, ajustable).
 *
 * Librería: Adafruit INA219 (Gestor de librerías del Arduino IDE).
 * Conexión I2C: SDA/SCL del INA219 a los del microcontrolador lector.
 *
 * Salida por Serial (115200): al terminar cada pulso imprime duración,
 * corriente activa promedio y pico, y la autonomía estimada.
 */
#include <Wire.h>
#include <Adafruit_INA219.h>

Adafruit_INA219 ina219;

// ---------- Parámetros ajustables ----------
const float CAPACITY_MAH   = 2000.0;   // capacidad de la batería objetivo
const float T_ACTIVE_S     = 2.279;    // tiempo activo por ciclo (medido)
const float T_CYCLE_S      = 120.0;    // ciclo de muestreo (del CSV)
const float I_SLEEP_UA     = 15.0;     // consumo en sueño (datasheet ESP32-C6)
const float THRESHOLD_MA   = 3.0;      // umbral para detectar el pulso activo
const uint32_t IDLE_END_MS = 300;      // ms por debajo del umbral = fin del pulso

// ---------- Estado de detección de pulso ----------
bool enPulso = false;
double sumaMa = 0;         // suma de lecturas durante el pulso
uint32_t nMuestras = 0;
float picoMa = 0;
uint32_t pulsoInicioMs = 0;
uint32_t ultimaSobreUmbralMs = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  if (!ina219.begin()) {
    Serial.println("[ERROR] No se encontro el INA219. Revisa el cableado I2C.");
    while (1) { delay(1000); }
  }
  // Rango fino para corrientes de decenas de mA (mejor resolucion)
  ina219.setCalibration_16V_400mA();
  Serial.println("INA219 listo. Midiendo corriente del nodo...");
  Serial.println("Espera a que el nodo despierte para ver el pulso activo.");
}

void reportarPulso() {
  float durS = (ultimaSobreUmbralMs - pulsoInicioMs) / 1000.0;
  float iActiva = (nMuestras > 0) ? (float)(sumaMa / nMuestras) : 0.0;

  // Corriente promedio del ciclo completo (activo + sueño) y autonomia
  float duty = T_ACTIVE_S / T_CYCLE_S;
  float iProm = iActiva * duty + (I_SLEEP_UA / 1000.0) * (1.0 - duty); // mA
  float autonomiaH = (iProm > 0) ? (CAPACITY_MAH / iProm) : 0.0;

  Serial.println("----- Pulso activo detectado -----");
  Serial.print("  Duracion:            "); Serial.print(durS, 3); Serial.println(" s");
  Serial.print("  Corriente activa:    "); Serial.print(iActiva, 2); Serial.println(" mA (promedio)");
  Serial.print("  Pico:                "); Serial.print(picoMa, 2); Serial.println(" mA");
  Serial.print("  I promedio del ciclo: "); Serial.print(iProm, 4); Serial.println(" mA");
  Serial.print("  Autonomia estimada:  "); Serial.print(autonomiaH, 0);
  Serial.print(" h ("); Serial.print(autonomiaH / 24.0, 1); Serial.println(" dias)");
  Serial.println("----------------------------------");
}

void loop() {
  float mA = ina219.getCurrent_mA();
  if (mA < 0) mA = -mA;   // el signo depende de la orientacion del INA219
  uint32_t ahora = millis();

  if (mA > THRESHOLD_MA) {
    if (!enPulso) {                  // inicio de pulso
      enPulso = true;
      sumaMa = 0; nMuestras = 0; picoMa = 0;
      pulsoInicioMs = ahora;
    }
    sumaMa += mA;
    nMuestras++;
    if (mA > picoMa) picoMa = mA;
    ultimaSobreUmbralMs = ahora;
  } else if (enPulso && (ahora - ultimaSobreUmbralMs) > IDLE_END_MS) {
    reportarPulso();                 // fin de pulso
    enPulso = false;
  }

  delay(1);   // ~1 kHz de muestreo, suficiente para el pulso de ~2,3 s
}

/*
 * Ejemplo de lo que verias en el Monitor Serie (valores ilustrativos):
 *   ----- Pulso activo detectado -----
 *     Duracion:            2.281 s
 *     Corriente activa:    38.60 mA (promedio)
 *     Pico:                92.10 mA
 *     I promedio del ciclo: 0.7477 mA
 *     Autonomia estimada:  2675 h (111.5 dias)
 *   ----------------------------------
 * -> ~3,7 meses, muy por encima del criterio de 8 h.
 * Pasa la "Corriente activa" y la "Autonomia" al documento.
 */
