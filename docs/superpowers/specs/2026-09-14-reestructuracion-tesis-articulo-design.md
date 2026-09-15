# Diseño de reestructuración de la tesis y corrección del artículo SmartSense

## 1. Propósito

Reestructurar el trabajo de grado SmartSense Monitoring como una monografía académica de la Universidad ECCI, en LaTeX y fuera del formato IEEE, y armonizar el artículo HardwareX con la misma base de evidencia. Ambos documentos deben explicar el diseño de ingeniería, responder las preguntas previsibles de sustentación y limitar cada afirmación a lo que realmente demuestran los registros, pruebas y archivos disponibles.

## 2. Decisiones aprobadas

- La tesis continuará en LaTeX y se entregará también como PDF compilado.
- El documento seguirá la topología de `TesisGradoSigechip.docx`: portada, preliminares, índices automáticos, capítulos académicos, conclusiones, referencias y anexos.
- `SmartSense_IEEE.tex` se conservará como respaldo histórico. La monografía tendrá un archivo maestro nuevo y fuentes separadas por capítulos.
- La tesis y el artículo compartirán cifras, definiciones y limitaciones. Ninguna métrica podrá tener interpretaciones incompatibles entre ambos textos.
- La contribución central será una plataforma abierta, local y reproducible para monitorización térmica superficial industrial. No se afirmará que el sistema implementa o valida mantenimiento predictivo.
- Los resultados no disponibles no se inventarán ni se presentarán como demostrados. Se distinguirán resultados medidos, inferidos, estimados y pendientes de caracterización.

## 3. Alcance

### 3.1 Trabajo de grado

Se creará `SmartSense_Trabajo_Grado.tex` como archivo maestro y el directorio `tesis/` con preliminares, capítulos y anexos. La monografía reutilizará y ampliará el contenido técnico vigente de `SmartSense_IEEE.tex`, las figuras existentes, los análisis reproducibles y la evidencia fotográfica.

La estructura será:

1. Portada institucional.
2. Página de presentación del trabajo de grado y tutores.
3. Agradecimientos.
4. Resumen, palabras clave, abstract y keywords.
5. Tabla de contenido.
6. Índice de figuras.
7. Índice de tablas.
8. Introducción.
9. Planteamiento del problema.
10. Objetivos general y específicos.
11. Estado del arte.
12. Marco de referencia.
13. Diseño metodológico.
14. Fundamentos y criterios de diseño.
15. Desarrollo e implementación.
16. Resultados y evidencias.
17. Discusión, limitaciones y amenazas a la validez.
18. Conclusiones y trabajo futuro.
19. Referencias bibliográficas.
20. Anexos.

### 3.2 Artículo HardwareX

Se actualizará `hardwarex/SmartSense_HardwareX.tex` sin cambiar la estructura obligatoria de HardwareX. Los ajustes abarcarán título y resumen, contexto, comparación técnica, validación térmica, autonomía, comunicación, observación del rodamiento, disponibilidad de archivos, capacidades, limitaciones y conclusiones. Se recompilará `hardwarex/SmartSense_HardwareX.pdf`.

### 3.3 Elementos fuera de alcance

- No se fabricarán nuevos nodos ni se alterará el hardware.
- No se declarará una calibración trazable sin un patrón de contacto calibrado y su certificado.
- No se declarará una descarga completa de batería cuando la evidencia corresponde a cinco días.
- No se declarará un alcance máximo porque el enlace seguía operativo a 688 m.
- No se declarará detección o pronóstico de falla de rodamiento sin etiquetado temporal independiente y un método de detección validado.
- No se creará una clase LaTeX institucional propia mientras no exista una plantilla oficial que la exija.

## 4. Diseño editorial del trabajo de grado

La monografía usará `report` sobre papel carta, una columna, Times New Roman o su equivalente métrico disponible en LaTeX, cuerpo de 11 puntos y márgenes aproximados al documento guía: 2,8 cm superior, 2,54 cm laterales y 2,35 cm inferior. Los preliminares usarán numeración romana y el cuerpo numeración arábiga. Los capítulos se numerarán automáticamente; las conclusiones, referencias y anexos aparecerán en la tabla de contenido.

Las figuras y tablas tendrán numeración por capítulo, título descriptivo, unidades, fuente cuando corresponda y referencia desde el texto. La tabla de contenido, el índice de figuras y el índice de tablas se generarán automáticamente. Se evitarán capturas de código como figuras cuando el contenido pueda referenciarse en el repositorio o resumirse mediante pseudocódigo, diagramas o fragmentos breves.

La tesis utilizará citas autor-fecha, coherentes con el documento guía, mediante una bibliografía central. El artículo conservará las referencias numeradas exigidas por HardwareX.

## 5. Arquitectura de contenido

### 5.1 Introducción y problema

El problema se formulará como la falta de monitorización térmica superficial continua, local y reproducible en equipos de una planta piloto cuando se depende de inspecciones puntuales o soluciones comerciales cerradas. Se explicará qué información se pierde con una lectura aislada y por qué una serie temporal aporta contexto sobre ciclos de operación, paradas y anomalías térmicas.

La pregunta de investigación se centrará en la viabilidad de diseñar e implementar una red inalámbrica de nodos PT100 que adquiera, transmita, almacene y visualice temperatura superficial sin depender de servicios de nube.

### 5.2 Objetivos

El objetivo general describirá el diseño, implementación y evaluación del sistema. Los objetivos específicos cubrirán:

- Selección e integración de la cadena PT100–MAX31865–ESP32-C6.
- Comunicación LoRa entre nodos y gateway.
- Persistencia local y dashboard de supervisión.
- Construcción y despliegue de tres nodos.
- Evaluación de comportamiento térmico, enlace, autonomía estimada y funciones del sistema.

Cada objetivo tendrá una correspondencia explícita con una sección de resultados y una conclusión.

### 5.3 Estado del arte y comparación

El estado del arte separará cuatro familias: redes inalámbricas de sensores, LoRa para monitorización industrial, sistemas abiertos de adquisición térmica y soluciones de monitorización de condición. La novedad se argumentará por integración y reproducibilidad, no por invención de LoRa, PT100 o ESP32.

La comparación con el Advantech WISE-2410 será funcional y normalizada. Incluirá apertura de hardware y software, variable primaria, vibración, tipo de enlace, almacenamiento, dependencia de nube, protección IP, autonomía, reproducibilidad y costo con fecha/tasa de cambio. Se reconocerán las ventajas del WISE-2410 —vibración, encapsulado IP66 y autonomía declarada— y las de SmartSense —PT100 externa, apertura, operación local y personalización—. El costo será una dimensión, no el argumento principal.

### 5.4 Fundamentos y criterios de ingeniería

Este capítulo responderá de manera explícita:

- **Por qué PT100:** estabilidad, uso industrial, rango, disponibilidad en montaje magnético, tres hilos y compatibilidad con MAX31865. La clase, fabricante y referencia solo se declararán con la evidencia disponible en BOM o ficha técnica.
- **Por qué LoRa:** baja tasa de datos, cobertura requerida, operación local, consumo y ausencia de cableado de comunicación.
- **Por qué 120 s:** compromiso entre resolución térmica, tasa de tramas, almacenamiento y energía, teniendo en cuenta que la temperatura de carcasa de maquinaria cambia más lentamente que variables como vibración.
- **Cómo se seleccionaron los componentes:** requisitos, alternativas, criterio elegido y limitaciones para microcontrolador, acondicionador, radio, batería, gateway, almacenamiento y gabinete.
- **Cómo se diseñó la alimentación:** batería LiPo, carga/protección, divisor resistivo, consumo por estados, pico de transmisión, ciclo de trabajo y corte de tensión.
- **Cómo se evaluó el enlace:** configuración del E220, frecuencia, potencia, velocidad aérea, condiciones de instalación, disponibilidad, PDR, RSSI y prueba de distancia.

### 5.5 Desarrollo e implementación

El flujo del sistema se documentará como:

`PT100 → MAX31865 → nodo ESP32-C6 → trama LoRa → gateway ESP32-C6 → microSD/LittleFS → pantalla y dashboard Wi-Fi`.

Se detallarán los tres nodos implementados, sus conexiones, trama de aplicación, ciclo de adquisición, sueño profundo, configuración de radio, gateway, organización de archivos y rotación de 30 días. Se explicará que el conjunto de 32 días se exportó antes de la rotación y que el Nodo B contiene además un intervalo más largo, sin mezclarlo con la ventana común de tres nodos.

El dashboard se describirá por funciones demostradas: temperatura, batería, estado de conexión, historial diario, estadísticas, comparación, nombres, intervalo, compensación y alarmas. La exportación CSV se marcará como no verificada si el firmware revisado no expone dicha función.

## 6. Política de evidencia y redacción

| Tema | Evidencia disponible | Formulación permitida | Formulación no permitida |
|---|---|---|---|
| Temperatura | Cinco pares de 65–71 °C frente al Fluke usado para ajustar el offset y corroboración FLIR | Consistencia o concordancia de campo; diferencia máxima observada de 0,4 °C | Exactitud real de ±0,4 °C; calibración; cumplimiento metrológico de ±1 °C |
| Incertidumbre | Especificaciones Fluke ±1,5 °C y FLIR ±2 °C; PT100 Clase B si se confirma documentalmente | La incertidumbre de las referencias supera el objetivo de 1 °C; se requiere patrón trazable | Incertidumbre combinada trazable del sistema sin presupuesto metrológico ni certificados |
| Autonomía | Descarga parcial de cinco días con celda de 1000 mAh y modelo de curva LiPo | Autonomía estimada: cota lineal y resultado del modelo, bajo el ciclo ensayado | Vida útil de meses demostrada experimentalmente; autonomía “medida” de 177 o 307 días |
| PDR | Inferencia por intervalos y ventana de 60 minutos; contador incorporado después | PDR estimado de 95,3–96,6 % en periodos activos; método y supuestos explícitos | PDR medido directamente en todo el despliegue de 32 días |
| Disponibilidad | Muestras recibidas y paradas simultáneas de los tres nodos | Disponibilidad cruda y disponibilidad del enlace durante gateway activo, por separado | Disponibilidad total del sistema superior al 97 % |
| Alcance | Enlace operativo a 688 m, NLOS urbano, RSSI cercano a −98 dBm | Alcance demostrado de al menos 688 m o límite inferior experimental | Alcance máximo de 688 m |
| Rodamiento | Serie térmica, imagen FLIR y asociación posterior durante inspección | Observación de campo o caso de estudio asociado posteriormente con una anomalía | Detección predictiva o causalidad demostrada |
| Despliegue | 51.338 muestras y ventana común de 32 días para tres nodos | Piloto o despliegue de duración extendida de 32 días | “Long-term” sin definición o justificación |

Toda cifra central indicará el método, unidad, población o periodo y limitación. Las tablas de criterios de aceptación no marcarán “cumple” cuando la métrica no fue medida con un método capaz de demostrar el criterio.

## 7. Preguntas de sustentación cubiertas

El documento incluirá una matriz de trazabilidad en un anexo que relacione cada pregunta con capítulo, figura, tabla o cálculo:

- Problema solucionado y alcance.
- Funcionamiento integral del sistema.
- Componentes y criterios de selección.
- Comunicación entre nodos y gateway.
- Función del gateway y del dashboard.
- Cantidad de nodos y datos recopilados.
- Comportamiento durante los 32 días.
- Justificación de PT100, LoRa y 120 s.
- Cálculo y limitaciones de autonomía.
- Evaluación de enlace, error e incertidumbre.
- Diseño de alimentación.
- Demostración y limitación del PDR.
- Interpretación del caso del rodamiento.
- Interpretación correcta de los 688 m.
- Ubicación de cálculos de diseño y evidencia reproducible.

## 8. Reproducibilidad

Los anexos documentarán la estructura del repositorio y enlazarán firmware, software, esquemáticos, CAD/STL, BOM, licencias, datos procesados y scripts de análisis existentes. El artículo no afirmará que existe un DOI hasta que se haya publicado efectivamente una versión en Zenodo. Mientras tanto, identificará el repositorio y el estado real de disponibilidad de los archivos.

La especificación de la PT100 se limitará a la información verificable. Si el fabricante o referencia comercial no aparece en la evidencia del proyecto, se declarará esa carencia como limitación de reproducibilidad en vez de inventar una identificación.

## 9. Entregables

- `SmartSense_Trabajo_Grado.tex` y módulos bajo `tesis/`.
- `SmartSense_Trabajo_Grado.pdf` compilado.
- Bibliografía central autor-fecha para la tesis.
- `hardwarex/SmartSense_HardwareX.tex` corregido.
- `hardwarex/SmartSense_HardwareX.pdf` recompilado.
- Script de compilación de la tesis para Windows.
- README actualizado con instrucciones de compilación y ubicación de entregables.
- PDFs finales organizados en `PDF_Para_Compartir/`, sin eliminar archivos previos del usuario.

## 10. Verificación y criterios de aceptación

El trabajo se considerará terminado cuando:

1. Ambos documentos compilen sin referencias, citas o etiquetas sin resolver.
2. La tesis sea de una columna y no use `IEEEtran`.
3. Portada, tabla de contenido, índice de figuras e índice de tablas aparezcan en el PDF.
4. Todas las preguntas de sustentación estén respondidas o identificadas honestamente como limitaciones.
5. Las cifras de nodos, muestras, fechas, disponibilidad, PDR, autonomía, RSSI, distancia y temperatura coincidan entre tesis y artículo.
6. “Predictive maintenance” solo aparezca para aclarar que no fue implementado ni validado, o en referencias bibliográficas pertinentes.
7. La autonomía se describa como estimación basada en una descarga parcial, no como vida útil demostrada.
8. La diferencia de 0,4 °C se describa como concordancia frente a referencias de campo, no como exactitud absoluta.
9. Los 688 m se describan como alcance mínimo demostrado en la prueba realizada.
10. La observación del rodamiento se presente como caso de campo y no como detección predictiva.
11. Las tablas y figuras sean legibles, estén numeradas y sean citadas desde el texto.
12. Los archivos finales estén disponibles en PDF y sus fuentes LaTeX queden versionadas.

## 11. Riesgos controlados

- **Ausencia de plantilla oficial ECCI:** se replica la estructura y presentación del documento guía sin afirmar que constituye una norma institucional.
- **Datos de validación incompletos:** la narrativa diferencia medición, estimación e inferencia.
- **Bibliografía duplicada entre documentos:** la tesis tendrá una fuente bibliográfica central y el artículo conservará su lista propia por requisitos editoriales.
- **Divergencia futura de cifras:** la revisión final usará una matriz de consistencia entre ambos documentos.
- **Cambios locales anteriores al pull:** permanecen recuperables en `stash@{0}` mientras se desarrolla la nueva versión.
