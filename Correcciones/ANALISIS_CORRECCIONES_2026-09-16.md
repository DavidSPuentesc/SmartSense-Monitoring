# Revisión de correcciones pendientes en la tesis y el artículo

Fecha de revisión: 16 de septiembre de 2026.
Documentos revisados: `SmartSense_Trabajo_Grado.tex` (carpeta `tesis/`) y `hardwarex/SmartSense_HardwareX.tex`, en el estado del commit `23d49ba` de `main`.

Fuentes de las correcciones:

1. Mensajes del tutor del 14 de septiembre (26 preguntas de sustentación, estructura tipo monografía, adaptar a la tesis lo corregido en el artículo).
2. `Correciones para articulo hardware x.docx` (cambio del párrafo de mantenimiento predictivo, contradicción del 0,4 °C, tabla frente al WISE-2410 y 15 preguntas).
3. `Correciones IA  a tener en cuenta.xlsx` (17 aspectos con criterio de cierre).
4. Tesis de ejemplo `TesisGradoSigechip.docx` (estructura de referencia).

## 1. Resumen

De las 17 filas del Excel, 8 quedan cerradas en el texto, 4 quedan documentadas pero sin cerrar el criterio, y 5 no se pueden cerrar escribiendo porque requieren una medición, un dato de compra o una publicación:

| Pendiente que no se resuelve escribiendo | Qué hace falta | Quién |
|---|---|---|
| Exactitud con referencia independiente | Termocupla o RTD calibrada, varios puntos, los tres nodos | Equipo, con laboratorio |
| Consumo real y autonomía | Correr el sketch INA219 y medir corriente por estado | Equipo |
| Identificación de la PT100 y del E220 | Fabricante, referencia, clase de la sonda; variante exacta del módulo y potencia configurada | Equipo (factura, foto del módulo, configuración) |
| Origen del 0,4 °C | Transcribir las 21 fotos de `figuras/adjuntos_mediciones/`: son el registro original (Fluke sobre el motor y monitor serie) | Santy |
| Repositorio con DOI y versión identificada | Etiquetar un release y depositar en Zenodo | Equipo |

Las 26 preguntas del tutor tienen sección y respuesta en la tesis, y el anexo "Matriz de preguntas para la sustentación" las recoge una por una. Tres respuestas son flojas y se detallan en la sección 4. La guía para cerrar cada pendiente está en la sección 8.

## 2. Excel de correcciones, fila por fila

| # | Aspecto | Estado | Dónde está | Qué falta para cumplir el criterio de cierre |
|---|---|---|---|---|
| 1 | Validación de temperatura con referencia independiente | Documentado, no cerrado | Tesis 8.4 (tabla de 5 parejas, MAE 0,06 °C), 9.2; artículo "Temperature field comparison" | La medición con referencia calibrada. Paso 7 |
| 2 | Autonomía con corriente medida | Documentado, no cerrado | Tesis 6.7 y 8.7 (dos modelos: 177 y 291 a 307 días); artículo "Estimated battery autonomy" | Medir corriente en sueño, adquisición y transmisión. El Excel cita un rango viejo (4,8 a 147 días) ya reemplazado. Paso 8 |
| 3 | Repositorio público + DOI + versión | Parcial | Artículo "Design files" cita el commit `5641bcd`; anexo A.2 de la tesis | No hay tag ni release, no hay DOI. Los CSV primarios no están en el repo. Paso 9 |
| 4 | Quitar "predictive maintenance" | Cerrado | Todo el texto habla de monitoreo de condición; "predictive" solo aparece negado | Nada |
| 5 | "Long-term" en título y abstract | Cerrado | Título nuevo: "an open-source wireless PT100 node and gateway for surface-temperature monitoring" | Nada |
| 6 | Separar disponibilidad del sistema y del enlace | Cerrado | Tesis tabla 8.3 (cruda 76,0 / 32,3 / 75,1 %; enlace 97,8 a 98,4 %); artículo tabla de disponibilidad | Nada |
| 7 | Rodamiento como observación de campo | Parcial | Tesis 8.8; artículo "Field observation (case study)" | El Excel pide una gráfica temporal temperatura, evento, inspección. No existe: faltan las fechas de la inspección y del mantenimiento. Paso 4 |
| 8 | Identificar la PT100 | No cerrado | Ambos documentos dicen que no se conoce fabricante ni clase | Buscar la referencia de compra o la ficha de la sonda. Paso 2 |
| 9 | Caracterizar el enlace (RSSI, SNR, distancia, configuración) | Parcial | Tesis 7.3 y 8.5; artículo "Range test" (688 m, −98 dBm, canal 23, 9600 bps, 2,4 kbps) | Potencia de transmisión y variante del E220 sin documentar. SNR no disponible en el módulo. Pasos 2 y 3 |
| 10 | Repetibilidad entre nodos | No cerrado | Tesis tabla 8.1 (estadísticas por nodo) y figura de comparación | Comparar los tres nodos en el mismo punto con la misma referencia. Se cierra con el paso 7 |
| 11 | Almacenamiento de 30 días vs. piloto de 32 | Cerrado en texto | Tesis 7.4.1 (rotación por fechas, exportación previa); artículo "Capabilities" | El Excel pide un diagrama del flujo de almacenamiento. Solo hay texto |
| 12 | Comparación justa con el WISE-2410 | Cerrado | Tesis tabla 4.2; artículo tabla "Functional comparison" | Falta la columna "Diferencia" que el tutor dibujó en el Word. Paso 6 |
| 13 | Estado del arte con trabajos recientes | Cerrado en la tesis, parcial en el artículo | Tesis cap. 4: Polonelli 2019, HIGROTERM 2021, Jakobsen 2024, Mois 2017, tabla comparativa; 21 referencias | El artículo conserva 12 referencias |
| 14 | Costos con fecha y tasa, separados por parte | Cerrado | Tesis 4.5; artículo BOM (627 000 COP, 149 000 por nodo, 130 000 gateway, 4 000 COP/USD) | Nada |
| 15 | Figuras más legibles | Sin cambios | Las figuras son las mismas de antes | Los scripts no se volvieron a correr porque los CSV no están en el repo |
| 16 | Cada afirmación con respaldo bibliográfico | Parcial | Bibliografía ampliada; matriz de sustentación | No hay matriz afirmación-referencia como tal |
| 17 | Una sola narrativa: monitoreo térmico abierto | Cerrado | Título, resumen e introducción de ambos documentos | Nada |

## 3. Word del tutor

- Párrafo de reemplazo sobre mantenimiento predictivo: no se usó textual, pero la idea está en la introducción y en el resumen de ambos documentos. Cerrado.
- "¿Cuál es su precisión absoluta? ¿Es superior a una solución comercial?": ambas respondidas en la matriz de sustentación y en el capítulo 9. La respuesta honesta es "no está establecida" y "no se demuestra superioridad". Cerrado en cuanto a redacción.
- Contradicción entre "±0,4 °C" y "≤1 °C requiere referencia calibrada": desapareció. En su lugar quedó una salvedad nueva: las cinco parejas transcritas dan máximo 0,1 °C y el 0,4 °C "no se reconstruye". Esa frase está en la tesis, en el artículo y en el README público. Se resuelve con el paso 1.
- Tabla frente al WISE-2410 sin centrarse en el costo: hecha en ambos documentos. Falta la columna "Diferencia".
- Las 15 preguntas del Word: todas tienen fila en la matriz de sustentación. Cinco se responden con "no se sabe todavía": exactitud real, autonomía real, potencia de transmisión, clase de la PT100 y consumo real.

## 4. Las 26 preguntas del tutor y dónde se responden en la tesis

| Pregunta | Sección | Estado |
|---|---|---|
| ¿Qué problema solucionan? | 2.1, 2.3 | Sólida |
| ¿Cómo funciona el sistema? | 7.1 | Sólida |
| ¿Qué componentes utilizaron? | 6.3 (tabla de selección), 7.2 | Sólida |
| ¿Cómo se comunican los nodos? | 7.3 | Sólida |
| ¿Qué hace el gateway? | 7.4 | Sólida |
| ¿Qué hace el dashboard? | 7.5 | Sólida |
| ¿Cuántos nodos implementaron? | 3, en todo el texto | Sólida |
| ¿Cuántos datos recopilaron? | 8.2 (51 338, con ventanas por nodo) | Sólida |
| ¿Qué ocurrió durante los 32 días? | 8.2, 8.3 | Floja: hay cobertura y huecos, pero no una cronología (fechas de paradas, fecha del evento del succionador, fecha de la inspección). Paso 4 |
| ¿Por qué PT100? | 5.2, 6.3 | Sólida |
| ¿Por qué LoRa? | 5.4, 6.3 | Sólida |
| ¿Por qué 120 s? | 6.4 | Aceptable: se justifica como compromiso; no hay constante de tiempo medida |
| ¿Cómo calcularon la autonomía? | 6.7, 8.7, anexo A.1 | Sólida |
| ¿Cómo evaluaron el enlace? | 6.5, 8.3, 8.5 | Sólida |
| ¿Cómo determinaron el error? | 6.6, 8.4 | Sólida |
| ¿Cómo seleccionaron los componentes? | 6.3 | Sólida |
| ¿Cómo diseñaron la alimentación? | 7.2.2, 6.7 | Sólida |
| ¿Cuál es la exactitud real? | 8.4, 9.2 | Respondida con "no establecida". Paso 7 |
| ¿Cuál es la incertidumbre? | 5.6, 9.2 | Respondida con "no hay presupuesto". Paso 7 |
| ¿Cómo demostraron el PDR? | 6.5.1, 8.3 | Respondida: estimado por intervalos, no directo |
| ¿Cómo demostraron la autonomía de meses? | 8.7 | Respondida: no se demostró, se estimó. Paso 8 |
| ¿Cómo saben que detectó la falla del rodamiento? | 8.8, 9.5 | Respondida: no lo saben, es observación de campo. Paso 4 |
| ¿688 m es el alcance máximo? | 8.5 | Respondida: no, cota inferior |
| ¿Dónde están los cálculos de diseño? | Anexo A.1, cap. 6 | Floja: hay cálculos de indicadores (muestras por día, ciclo de trabajo, autonomía, PDR) pero no de dimensionamiento (divisor de batería, elección de R_ref, bytes de almacenamiento, presupuesto de enlace). Paso 3 |
| ¿Qué criterios de ingeniería usaron? | 6.3 | Sólida |
| Tabla de contenido, figuras, etc. | Preliminares | Sólida (índice, lista de figuras y de tablas automáticos) |

## 5. Estructura frente a la tesis de ejemplo

| Tesis de ejemplo (Sigechip) | Tesis SmartSense | Observación |
|---|---|---|
| Portada institucional y de presentación | Igual | Ok |
| Agradecimientos | Igual | Ok |
| Índice, índice de figuras | Índice, figuras y tablas | Ok |
| Resumen (después del índice) | Resumen y abstract (antes del índice) | Orden distinto; menor |
| Introducción sin numerar | Capítulo 1 numerado | Menor |
| 1 Planteamiento: descripción, formulación | 2 Necesidad, pregunta, justificación y alcance | Equivalente |
| 2 Objetivos | 3 Objetivos + criterios de verificación | Ok |
| 3 Estado del arte | 4 Estado del arte | Ok |
| 4 Marco de referencia | 5 Marco de referencia | Ok |
| 5 Diseño metodológico: tipo de investigación, enfoque, población y muestra, técnicas e instrumentos, procedimiento | 6 Enfoque, etapas, requisitos, periodo, protocolo, comparación, energía | Falta usar los nombres de subsección que espera la ECCI. Paso 5 |
| 6 Fundamentos | No existe | El propio plan de reestructuración lo tenía como capítulo ("Fundamentos y criterios de diseño") y no se creó. Es justo donde caben los cálculos de diseño y los criterios de selección que pregunta el tutor. Paso 3 |
| 7 Desarrollo e implementación | 7 | Ok |
| 8 Resultados y evidencias | 8 | Ok |
| (no tiene) | 9 Discusión y limitaciones | Adición razonable |
| Conclusiones sin numerar | 10 numerado | Menor |
| Referencias, Anexos | Igual | Ok |

## 6. Redacción

La tesis repite la misma fórmula de salvedad muchas veces: 86 construcciones del tipo "no demuestra", "no acredita", "no constituye", "no sustituye" en unas 20 000 palabras, y 7 de las 33 leyendas de figura terminan aclarando lo que la figura no prueba. Cada limitación es cierta y el tutor pidió que quedaran explícitas, pero repetidas en cada párrafo hacen que el lector dude de todo, incluso de lo que sí se hizo.

Reescritos el 16 de septiembre con ese criterio (misma información, una limitación dicha una vez y en su sitio): resumen y abstract, introducción, conclusiones y las siete leyendas. Queda pendiente aplicar el mismo criterio a los capítulos 2, 4 a 9 y anexos; conviene hacerlo capítulo por capítulo y con el visto bueno del tutor sobre el tono.

Artículo: el abstract termina con "absent primary CSV files and incomplete component identification limit full replication of the evaluation". Esa frase pertenece a la sección de limitaciones, no al abstract. Propuesta de cierre: "Hardware, firmware, software and analysis scripts are released under open-source licenses in a public repository."

## 7. Orden sugerido

1. Paso 1, el 0,4 °C (Santy, con las fotos).
2. Paso 2, identificar PT100 y E220.
3. Paso 3, capítulo de fundamentos con los cálculos.
4. Paso 4, cronología del piloto.
5. Pasos 5 y 6, títulos de metodología y columna "Diferencia".
6. Pasos 7 y 8, mediciones nuevas (exactitud, corriente); mientras tanto siguen como trabajo futuro.
7. Paso 9, release y DOI, de último.
8. Paso 10, tono del resto de capítulos, después de validar con el tutor.

## 8. Paso a paso para cerrar cada pendiente

### Paso 1. Resolver el 0,4 °C con las fotos de mediciones

Quién: Santy. Esfuerzo: una o dos horas.

Las 21 fotos de `figuras/adjuntos_mediciones/` son el registro original. Contienen dos cosas: fotos del Fluke 62 MAX apuntando al motor del succionador (se ven lecturas de 62,0 y 62,6 °C, que no están entre las cinco parejas transcritas de 65 a 71 °C) y capturas del monitor serie con los arranques (2,279 s en la mayoría y al menos uno de 2,277 s).

1. En el teléfono, abrir las fotos originales en la galería y anotar fecha y hora de cada una. Las copias del repo perdieron esos datos al pasar por WhatsApp.
2. Separar las fotos del Fluke de las capturas del monitor serie.
3. Para cada foto del Fluke, buscar en el CSV del nodo A la muestra más cercana en el tiempo (dos minutos de tolerancia) y anotar: hora, SmartSense, Fluke, diferencia absoluta.
4. Recalcular la media absoluta y el máximo con todas las parejas.
5. Si el máximo da 0,4 °C: reemplazar la tabla de cinco filas por la completa en la tesis 8.4 y en el artículo, y borrar la frase "no se reconstruye" en la tesis (8.4, 9.2 y matriz del anexo), en el artículo (comparación, leyenda de la tabla y "Capabilities") y en el README.
6. Si el máximo sigue en 0,1 °C: cambiar `\DiferenciaTermicaMaxima` a `0.1` en `tesis/datos-evidencia.tex` y borrar las mismas frases.
7. Con la captura de 2,277 s, completar la frase de la sección 8.6 que dice que los dos valores distintos "no se transcriben".
8. Guardar la tabla como `analisis/parejas_fluke.csv` para que el cálculo sea reproducible.

### Paso 2. Identificar la sonda PT100, el módulo E220 y la potencia de transmisión

Quién: equipo. Esfuerzo: una hora, más lo que tarde el proveedor.

Estado al 17 de septiembre: hecho para el radio y la batería. El módulo es el E220-400T22D (LLCC68; canal 23 = 433,125 MHz; 22 dBm, código 0, valor de fábrica y el que fija la rutina de configuración del firmware; sensibilidad de ficha −127 dBm típica a 2,4 kbps, margen de unos 29 dB frente al RSSI de −98 dBm). La celda de los nodos es de 1000 mAh, la misma del ensayo de descarga, así que desapareció la distinción con los "2000 mAh de la BOM". Todo eso ya está en la BOM, en CONEXIONES.md, en la tesis, en el artículo (referencia [13]) y en el README. Sigue pendiente la PT100: solo se sabe que es de tres hilos.

Estado al 23 de septiembre: la PT100 quedó identificada por su origen de compra (AliExpress, artículo 1005008619909588, rango declarado −60 a +200 °C); el vendedor no indica fabricante ni clase, así que el presupuesto de incertidumbre mantiene la clase B como supuesto declarado.

1. Fotografiar la serigrafía del módulo de radio. Las variantes de 433 MHz de Ebyte se llaman E220-400T22D (22 dBm) y E220-400T30D (30 dBm); el sufijo dice la potencia máxima.
2. Leer la configuración guardada en el radio con la herramienta de Ebyte por USB-TTL o con el modo de configuración descrito en el manual del modelo identificado: potencia, canal, tasa aérea y dirección. El nodo no reconfigura el radio al arrancar, así que lo que tenga guardado es lo que se usó en el piloto.
3. Para la PT100, buscar la factura o el pedido del distribuidor. Si no hay ficha, hacer una verificación en baño de hielo: medir la resistencia a 0 °C con un multímetro de cuatro hilos. La clase B admite 100 ± 0,12 Ω y la clase A 100 ± 0,06 Ω. No demuestra la clase, pero descarta una sonda fuera de tolerancia.
4. Escribir lo encontrado en `hardware/BOM.csv`, en la tesis 7.2 y en la tabla de la BOM del artículo, y quitar las frases que dicen que no se identificó.

### Paso 3. Crear el capítulo "Fundamentos y criterios de diseño"

Quién: equipo. Esfuerzo: medio día.

1. Crear `tesis/capitulos/06b-fundamentos.tex` con `\chapter{Fundamentos y criterios de diseño}` y agregar `\input{tesis/capitulos/06b-fundamentos}` en `SmartSense_Trabajo_Grado.tex`, entre el capítulo 6 y el 7.
2. Mover allí la tabla de selección de componentes del capítulo 6, dejando en metodología una referencia a ella.
3. Escribir las secciones de cálculo. Los números salen de datos que ya están en el texto:

   Divisor de batería. Con R1 = 344,8 kΩ y R2 = 994,3 kΩ, el ADC recibe V_bat × R2 / (R1 + R2) = 4,2 × 0,7425 = 3,12 V con la celda llena, por debajo de los 3,3 V de fondo de escala. El divisor consume 4,2 V / 1,339 MΩ = 3,1 µA, despreciable frente a la corriente de sueño.

   Referencia del MAX31865. R_ref = 421,1 Ω, cerca de cuatro veces la resistencia nominal de la PT100, que es lo que recomienda el fabricante. La resolución es 421,1 Ω / 32 768 = 0,0129 Ω por cuenta, unos 0,033 °C a 0,385 Ω/°C. Aclarar que es resolución, no exactitud.

   Muestreo y almacenamiento. 86 400 s / 120 s = 720 muestras por nodo y día. El gateway escribe cada muestra como "segundo del día, temperatura × 10" y salto de línea (formato `%lu,%d`), entre 10 y 11 bytes. Eso da unos 7,5 kB por nodo y día, unos 680 kB para 30 días y tres nodos, más un clúster del sistema de archivos por cada uno de los 90 archivos. Cabe en cualquier microSD y en la partición LittleFS.

   Energía. I_media = D × I_activa + (1 − D) × I_sueño, con D = 2,279 / 120 = 1,9 %. Con la corriente activa implicada por el modelo (11,7 mA) y 15 µA de sueño: 0,222 + 0,015 = 0,237 mA, y 1000 mAh / 0,237 mA = 4 220 h = 176 días, que coincide con el modelo lineal. Marcar qué entradas son medidas (D, tensiones) y cuáles supuestas (corrientes) hasta que se haga el paso 8.

   Enlace. Pérdida en espacio libre a 433 MHz y 688 m: 20 log(688) + 20 log(433) − 27,55 = 81,9 dB. La caída observada entre el punto cercano (−15 dBm) y el lejano (−98 dBm) fue de 83 dB. El margen se calcula como RSSI observado menos la sensibilidad de la ficha a 2,4 kbps; poner ese valor cuando el paso 2 identifique el módulo.

   Umbrales de alarma. Escribir con qué criterio se fijaron los umbrales de advertencia y crítico en el piloto.

4. Revisar el anexo A.1: los cálculos de indicadores pueden quedarse allí o moverse al capítulo nuevo, pero no repetirse.

### Paso 4. Cronología del piloto

Quién: equipo, con una llamada a la planta. Esfuerzo: dos horas.

Estado al 18 de septiembre: hecho. La cronología se reconstruyó del CSV del piloto con `analisis/cronologia.py` (tabla y figura en resultados): paradas del succionador con la sonda a temperatura ambiente, cuatro periodos sin registro del gateway simultáneos en los tres nodos, y el máximo de 70,0 °C el 23 de mayo. El equipo confirmó que el rodamiento se intervino en las paradas del 30 y 31 de mayo y del 7 y 8 de junio, después del máximo; así quedó en la tesis, el artículo y la figura.

1. Armar una tabla de eventos con fecha, evento y fuente: 8 de mayo inicio de A y C; 10 de mayo inicio de B; huecos simultáneos (fechas que imprime `analisis/figuras_validacion.py` al correrlo con el CSV); fecha y hora del máximo de 70,0 °C del nodo A (del CSV); fecha de las fotos del Fluke y de la imagen FLIR (galería del teléfono); fecha de la inspección y del cambio de rodamiento (preguntar en planta); 9 de junio fin de A y C; 20 de julio fin de B; 13 a 18 de agosto descarga parcial; 20 de agosto prueba de alcance.
2. Guardarla como `analisis/cronologia.csv` y generar una línea de tiempo con matplotlib en `figuras/cronologia.pdf`.
3. Agregar en la sección 8.2 una subsección "Qué ocurrió durante el piloto" con la tabla y la figura.
4. En la sección 8.8, usar la parte del succionador (temperatura, imagen térmica, inspección) como la gráfica temporal que pide el Excel. Si la planta no da la fecha de la inspección, decirlo en la tabla en vez de dejar el hueco sin explicar.

### Paso 5. Subsecciones de metodología con los nombres de la ECCI

Quién: equipo. Esfuerzo: media hora. Solo son títulos sobre contenido que ya existe.

1. "Tipo de investigación": aplicada, con componente experimental (está en 6.1).
2. "Enfoque": cuantitativo, con observación de campo.
3. "Población y muestra": tres puntos de medición sobre equipos de la planta; 51 338 muestras con sus ventanas por nodo.
4. "Técnicas e instrumentos de recolección": PT100 y MAX31865, Fluke 62 MAX, FLIR C8, scripts de análisis.
5. "Procedimiento": la tabla de etapas que ya existe.

### Paso 6. Columna "Diferencia" en las dos tablas frente al WISE-2410

Quién: equipo. Esfuerzo: veinte minutos.

Agregar una cuarta columna con quién queda mejor en cada fila, como la dibujó el tutor: hardware abierto, SmartSense; PT100 externa, SmartSense; LoRa frente a LoRaWAN, distinto; vibración, WISE; historial local, SmartSense; dependencia de nube, SmartSense; grado IP, pendiente en SmartSense; batería, WISE por especificación y SmartSense pendiente de medir; costo, ver la BOM. Aplicarlo en la tesis (tabla 4.2) y en el artículo.

### Paso 7. Exactitud con una referencia independiente

Quién: equipo. Esfuerzo: un día de laboratorio más el préstamo del instrumento. Cierra también la repetibilidad entre nodos.

1. Conseguir un termómetro de contacto con certificado de calibración vigente (laboratorio de la ECCI o un laboratorio de metrología): termocupla tipo K o T, o RTD patrón.
2. Versión mínima defendible antes de la sustentación: tres puntos (ambiente, unos 45 °C y unos 70 °C) con placa caliente o baño térmico, los tres nodos y cinco parejas por punto y nodo. Son 45 parejas.
3. No usar ese instrumento para ajustar el offset. Dejar el offset como estaba, o ajustarlo con parejas separadas de las de verificación.
4. Calcular por nodo: media con signo, MAE, RMSE, máximo y desviación. Estimar la incertidumbre con el certificado, la resolución y la repetibilidad.
5. Reportar en la sección 8.4 con una tabla nueva, actualizar las macros de temperatura y `\EstadoTemperatura` en `tesis/datos-evidencia.tex`, y quitar en el capítulo 9 la frase de que la repetibilidad no está demostrada.

### Paso 8. Consumo real y autonomía

Quién: equipo. Esfuerzo: medio día de medición.

Estado al 17 de septiembre: como el prototipo se entregó al cliente, se hizo lo que sí se puede sin hardware: un modelo de consumo por estados con valores de ficha (ESP32-C6, XIAO, E220-400T22D) y los tiempos del firmware, con Monte Carlo (`analisis/modelo_consumo.py`, figura `modelo-consumo.pdf`). Da 47 días (42 a 54), entre 3,5 y 6,4 veces más consumo del que implica la descarga observada (177 a 307 días). Quedó en la tesis como subsección 8.7 con tabla de entradas, en discusión, conclusiones y una frase en el artículo, presentado como estimación que acota la autonomía entre mes y medio y diez meses. También se comprobó que el radio debe estar durmiendo (si no, la celda duraría 2,4 días). La medición de corriente sigue siendo la única forma de cerrar el intervalo.

1. Correr el sketch de `analisis/medicion_corriente_INA219/` durante al menos cien ciclos para capturar el pulso activo: corriente media y duración.
2. Medir la corriente de sueño aparte: el INA219 no resuelve bien 15 µA. Usar un multímetro en escala de microamperios en serie con la batería con el nodo dormido, o el shunt de 1 Ω con osciloscopio del montaje de la figura de metodología.
3. Calcular la corriente media y la autonomía con la ecuación del paso 3 y comparar con 177 y 291 a 307 días.
4. Una descarga completa hasta 3,30 V con la celda de 1000 mAh tomaría unos 170 días a 0,24 mA, así que no cabe antes de la sustentación. Decirlo así en el texto.
5. Actualizar la sección 8.7, las macros de autonomía y `\EstadoAutonomia`.

### Paso 9. Release del repositorio y DOI

Quién: equipo. Esfuerzo: una hora, al final.

Estado al 22 de septiembre: hecho. El repositorio se transfirió a la cuenta DavidSPuentesc, se publicó el release `v1.0-tesis` y Zenodo generó el DOI de concepto 10.5281/zenodo.22903028 (versión: 10.5281/zenodo.22903029). El DOI y la nueva URL quedaron en artículo, tesis, README y licencias. Pendiente: rotar la clave WiFi del historial y publicar `v1.1-tesis` con el DOI ya escrito en los documentos.

1. Cambiar la contraseña de la red WiFi vieja, que sigue en el historial de git.
2. Sacar del repositorio las carpetas `.superpowers/` y `docs/superpowers/` y agregar `.superpowers/` al `.gitignore`.
3. Agregar los CSV de análisis (parejas, cronología y, si la planta lo permite, el histórico exportado) y la identificación de componentes en la BOM.
4. Crear la etiqueta `v1.0-tesis`, publicar un release en GitHub y activar la integración de Zenodo con GitHub para obtener el DOI.
5. Poner el DOI en la tabla de especificaciones del artículo y en el anexo A.2 de la tesis, y quitar "No archived DOI is documented".

### Paso 10. Tono del resto de capítulos

Quién: equipo, con el tutor. Esfuerzo: una tarde por capítulo.

1. Mostrar al tutor el resumen, la introducción y las conclusiones ya reescritas y confirmar que ese es el tono que quiere.
2. Aplicar el mismo criterio a los capítulos 2, 4 a 9 y anexos: cada limitación se dice una vez, en discusión, y las leyendas describen la figura.
3. Recompilar y revisar índices y referencias cruzadas después de cada capítulo.
