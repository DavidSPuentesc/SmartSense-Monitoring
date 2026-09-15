# SmartSense Thesis and Article Restructuring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a non-IEEE, one-column ECCI-style SmartSense thesis and a technically consistent HardwareX article, with both final PDFs compiled from versioned LaTeX sources.

**Architecture:** Preserve `SmartSense_IEEE.tex` as historical input, create a modular `report`-class thesis under `tesis/`, and treat a shared evidence ledger as the source of truth for every quantitative claim. Update the existing HardwareX source against that ledger, compile both outputs with Tectonic, and run automated text and LaTeX consistency checks before copying the final PDFs to the delivery folder.

**Tech Stack:** LaTeX `report`, Tectonic 0.16.9, PowerShell, BibTeX/natbib, existing Python analysis scripts, Git.

**Spec:** `docs/superpowers/specs/2026-09-14-reestructuracion-tesis-articulo-design.md`

## Global Constraints

- The thesis must use LaTeX and deliver a compiled PDF.
- The thesis must be one-column and must not use `IEEEtran`.
- `SmartSense_IEEE.tex` remains unchanged as a historical source.
- The main contribution is open-source, local industrial surface-temperature monitoring; predictive maintenance is not implemented or validated.
- The 0.4 °C result is field agreement, not absolute accuracy or traceable calibration.
- The five-day discharge supports an autonomy estimate, not a demonstrated months-long lifetime.
- The 32-day PDR is inferred from arrival intervals unless a sent-versus-received sequence dataset is available.
- The 688 m test is a demonstrated lower bound, not maximum range.
- The bearing event is a field observation associated after inspection, not validated predictive detection.
- Existing user files in `PDF_Para_Compartir/` must not be deleted or overwritten without retaining the prior version.
- Thesis and article figures, values, dates, units, and limitations must agree.

---

## Planned File Structure

```text
SmartSense_Trabajo_Grado.tex              # Thesis master, formatting and include order
compilar_trabajo_grado.ps1                # Reproducible thesis build
tesis/
  datos-evidencia.tex                     # Shared numeric macros and evidence wording
  referencias.bib                         # Author-date thesis bibliography
  preliminares/
    portada.tex                            # ECCI cover and submission page
    agradecimientos.tex                   # Acknowledgements
    resumen.tex                            # Spanish abstract and English abstract
  capitulos/
    01-introduccion.tex                    # Context, contribution and document map
    02-planteamiento-problema.tex          # Problem statement and research question
    03-objetivos.tex                       # General and specific objectives
    04-estado-arte.tex                     # Related work and comparison
    05-marco-referencia.tex                # Concepts and engineering foundations
    06-diseno-metodologico.tex             # Research method and evaluation protocol
    07-desarrollo-implementacion.tex       # Nodes, link, gateway, storage and dashboard
    08-resultados-evidencias.tex           # Measured, inferred and estimated results
    09-discusion-limitaciones.tex          # Interpretation and validity limitations
    10-conclusiones.tex                    # Objective-linked conclusions and future work
  anexos/
    anexos.tex                             # Calculations, reproducibility and defense matrix
hardwarex/SmartSense_HardwareX.tex         # Corrected article
hardwarex/compilar_hardwarex.ps1           # Reproducible article build
README.md                                  # Updated document/build map
COMPILAR_LATEX.md                          # Build and verification instructions
PDF_Para_Compartir/                        # Final thesis and article PDFs
```

### Task 1: Establish the shared evidence ledger

**Files:**
- Create: `tesis/datos-evidencia.tex`
- Reference: `SmartSense_IEEE.tex`
- Reference: `hardwarex/SmartSense_HardwareX.tex`
- Reference: `Correcciones/Correciones IA  a tener en cuenta.xlsx`
- Reference: `Correcciones/Correciones para articulo hardware x.docx`

**Interfaces:**
- Consumes: Results already present in the current thesis, article, figures and analysis scripts.
- Produces: Stable LaTeX macros used by thesis chapters and checked against the article.

- [ ] **Step 1: Create the evidence macro file with the accepted values**

```latex
% Central evidence ledger. Do not redefine these values in thesis chapters.
\newcommand{\NumeroNodos}{3}
\newcommand{\TotalMuestras}{51\,338}
\newcommand{\DuracionComunDias}{32}
\newcommand{\PeriodoNominal}{120~s}
\newcommand{\PeriodoMediano}{121~s}
\newcommand{\DisponibilidadEnlace}{97.8--98.4\,\%}
\newcommand{\PDREstimado}{95.3--96.6\,\%}
\newcommand{\PerdidaEstimada}{3.5--4.7\,\%}
\newcommand{\DiferenciaTermicaMaxima}{0.4~\textdegree C}
\newcommand{\DiferenciaTermicaMedia}{0.1~\textdegree C}
\newcommand{\DuracionDescarga}{5~días}
\newcommand{\AutonomiaLinealEstimada}{177~días}
\newcommand{\AutonomiaModeloEstimada}{291--307~días}
\newcommand{\DistanciaDemostrada}{688~m}
\newcommand{\RSSILejano}{-98~dBm}
\newcommand{\TiempoActivo}{2.279~s}
\newcommand{\CicloTrabajo}{1.9\,\%}
```

- [ ] **Step 2: Verify every macro against the source and analysis files**

Run:

```powershell
rg -n "51338|51.338|32 días|121|97.8|98.4|95.3|96.6|3.45|4.68|0.4|0.1|177|291|307|688|-98|2.279|1.9" SmartSense_IEEE.tex hardwarex/SmartSense_HardwareX.tex analisis
```

Expected: every ledger value has at least one traceable source; discrepancies are resolved by checking the generating analysis script rather than averaging conflicting values.

- [ ] **Step 3: Add four wording constants for evidence status**

```latex
\newcommand{\EstadoTemperatura}{concordancia de campo, no calibración metrológica}
\newcommand{\EstadoAutonomia}{estimación basada en una descarga parcial de cinco días}
\newcommand{\EstadoPDR}{estimación por intervalos de llegada durante periodos activos}
\newcommand{\EstadoAlcance}{cota inferior con el enlace todavía operativo}
```

- [ ] **Step 4: Check the file for accidental overclaims**

Run:

```powershell
rg -n -i "exactitud.*0[.,]4|autonomía medida.*177|alcance máximo|detección predictiva" tesis/datos-evidencia.tex
```

Expected: no matches.

- [ ] **Step 5: Commit the evidence ledger**

```powershell
git add tesis/datos-evidencia.tex
git commit -m "docs: centralizar evidencia cuantitativa de SmartSense"
```

### Task 2: Create the thesis master, front matter and reproducible build

**Files:**
- Create: `SmartSense_Trabajo_Grado.tex`
- Create: `tesis/preliminares/portada.tex`
- Create: `tesis/preliminares/agradecimientos.tex`
- Create: `tesis/preliminares/resumen.tex`
- Create: `compilar_trabajo_grado.ps1`
- Test: `SmartSense_Trabajo_Grado.pdf`

**Interfaces:**
- Consumes: `tesis/datos-evidencia.tex` macros.
- Produces: The document class, formatting, front matter and include contract used by every chapter.

- [ ] **Step 1: Write the master document preamble and include order**

Use this foundation:

```latex
\documentclass[11pt,letterpaper,oneside]{report}
\usepackage[utf8]{inputenc}
\usepackage[T1]{fontenc}
\usepackage[spanish,es-nodecimaldot]{babel}
\usepackage{newtxtext,newtxmath}
\usepackage[top=2.8cm,bottom=2.35cm,left=2.54cm,right=2.54cm]{geometry}
\usepackage{setspace,graphicx,booktabs,tabularx,array,longtable}
\usepackage{amsmath,amssymb,siunitx,xcolor,url}
\usepackage[hidelinks]{hyperref}
\usepackage[round,authoryear]{natbib}
\graphicspath{{figuras/}}
\sisetup{output-decimal-marker={,}}
\input{tesis/datos-evidencia}
\begin{document}
\pagenumbering{gobble}
\input{tesis/preliminares/portada}
\input{tesis/preliminares/agradecimientos}
\input{tesis/preliminares/resumen}
\pagenumbering{roman}
\tableofcontents
\listoffigures
\listoftables
\clearpage
\pagenumbering{arabic}
\input{tesis/capitulos/01-introduccion}
\input{tesis/capitulos/02-planteamiento-problema}
\input{tesis/capitulos/03-objetivos}
\input{tesis/capitulos/04-estado-arte}
\input{tesis/capitulos/05-marco-referencia}
\input{tesis/capitulos/06-diseno-metodologico}
\input{tesis/capitulos/07-desarrollo-implementacion}
\input{tesis/capitulos/08-resultados-evidencias}
\input{tesis/capitulos/09-discusion-limitaciones}
\input{tesis/capitulos/10-conclusiones}
\bibliographystyle{apalike}
\bibliography{tesis/referencias}
\appendix
\input{tesis/anexos/anexos}
\end{document}
```

- [ ] **Step 2: Write the two institutional title pages**

Include exactly these identity fields:

```latex
SMARTSENSE MONITORING: SISTEMA INALÁMBRICO PARA EL MONITOREO Y ANÁLISIS HISTÓRICO DE TEMPERATURA EN EQUIPOS INDUSTRIALES

Jesus Andre Bojaca Lopez
David Santiago Puentes Cárdenas

Universidad ECCI
Facultad de Ingenierías
Programa de Ingeniería Electrónica
Bogotá D. C.
2026

Trabajo de grado presentado como requisito para optar al título de Ingeniero Electrónico

Tutores:
Jhon Edwin Vera
Sergio Francisco Mora Martínez
```

- [ ] **Step 3: Write acknowledgements, Spanish summary and English abstract**

The summaries must state: open/local platform, PT100/MAX31865/ESP32-C6/E220 architecture, three nodes, 51,338 samples, 32-day common deployment, link availability during gateway-active periods, and evidence limitations. Use `monitoreo de condición` / `condition monitoring`; do not claim predictive maintenance.

- [ ] **Step 4: Write the thesis build script**

Base it on `compilar_ieee.ps1`, but set:

```powershell
$source = Join-Path $root 'SmartSense_Trabajo_Grado.tex'
$expectedPdf = Join-Path $root 'SmartSense_Trabajo_Grado.pdf'
```

Run Tectonic once; when falling back to `pdflatex`, run `pdflatex`, `bibtex SmartSense_Trabajo_Grado`, then `pdflatex` twice. Throw if the compiler exit code is non-zero or the expected PDF does not exist.

- [ ] **Step 5: Add minimal chapter stubs required for the first compilation**

Each planned chapter file must contain its final `\chapter{...}` title and one scope sentence, for example:

```latex
\chapter{Introducción}
Este capítulo presenta el contexto, la contribución y la organización del trabajo.
```

The annex file must start with `\chapter{Información complementaria y reproducibilidad}`.

- [ ] **Step 6: Compile and inspect the front matter**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\compilar_trabajo_grado.ps1
```

Expected: `SmartSense_Trabajo_Grado.pdf` exists; the log has no `Undefined control sequence`, `LaTeX Error`, or missing-file error.

- [ ] **Step 7: Commit the thesis shell**

```powershell
git add SmartSense_Trabajo_Grado.tex compilar_trabajo_grado.ps1 tesis/preliminares tesis/capitulos tesis/anexos
git commit -m "docs: crear estructura monografica del trabajo de grado"
```

### Task 3: Build the author-date bibliography and academic foundations

**Files:**
- Create: `tesis/referencias.bib`
- Modify: `tesis/capitulos/01-introduccion.tex`
- Modify: `tesis/capitulos/02-planteamiento-problema.tex`
- Modify: `tesis/capitulos/03-objetivos.tex`
- Modify: `tesis/capitulos/04-estado-arte.tex`
- Modify: `tesis/capitulos/05-marco-referencia.tex`
- Test: `SmartSense_Trabajo_Grado.pdf`

**Interfaces:**
- Consumes: Current bibliography in `SmartSense_IEEE.tex` and the approved contribution framing.
- Produces: Cite keys and conceptual definitions used by methodology, development and discussion chapters.

- [ ] **Step 1: Convert the current references into BibTeX entries**

Define at least these stable keys with complete authors, title, venue, year, volume, issue and pages where available:

```text
akyildiz2002, yick2008, centenaro2016, semtech2015, augustin2016,
bor2016, polonelli2019, jakobsen2024, cinar2020, jardine2006,
higroterm2021, wise2410, max31865, e220manual, xiaoc6, iec60751
```

- [ ] **Step 2: Write the introduction around the demonstrated contribution**

The closing contribution paragraph must say that SmartSense integrates industrial RTD acquisition, point-to-point LoRa, autonomous power and a local dashboard, and evaluates them in a pilot deployment. It must explicitly say that the system supports condition monitoring but does not implement a predictive-maintenance algorithm.

- [ ] **Step 3: Write the problem statement and research question**

Use the research question:

```text
¿Es viable diseñar e implementar una red inalámbrica, abierta y de operación local que adquiera, transmita, almacene y visualice la temperatura superficial de equipos industriales, conservando un historial útil para analizar su comportamiento térmico?
```

- [ ] **Step 4: Write measurable objectives**

The general objective is to design, implement and evaluate SmartSense. The five specific objectives cover sensor-chain integration, LoRa communication, gateway/dashboard, construction of three nodes, and evaluation of thermal behaviour, link performance and energy operation.

- [ ] **Step 5: Write the state of the art and normalized comparison table**

Include columns for system, sensor/variable, communication, local/cloud storage, openness, validation and limitation. Add a separate SmartSense-versus-WISE-2410 table that recognizes WISE vibration, IP66 enclosure and specified two-year autonomy, while identifying SmartSense openness, external PT100 and local dashboard.

- [ ] **Step 6: Write the reference framework**

Cover RTD/PT100 and IEC 60751, three-wire compensation, MAX31865 conversion, LoRa versus LoRaWAN, PDR versus availability, RSSI/SNR, deep sleep and duty cycle, field agreement versus calibration, measurement error and uncertainty.

- [ ] **Step 7: Compile citations and verify their resolution**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\compilar_trabajo_grado.ps1
rg -n "Citation.*undefined|There were undefined references|LaTeX Error" SmartSense_Trabajo_Grado.log
```

Expected: the compilation succeeds and the scan returns no matches.

- [ ] **Step 8: Commit the academic foundation chapters**

```powershell
git add tesis/referencias.bib tesis/capitulos/01-introduccion.tex tesis/capitulos/02-planteamiento-problema.tex tesis/capitulos/03-objetivos.tex tesis/capitulos/04-estado-arte.tex tesis/capitulos/05-marco-referencia.tex SmartSense_Trabajo_Grado.pdf
git commit -m "docs: desarrollar fundamentos academicos de la tesis"
```

### Task 4: Document methodology, design calculations and implementation

**Files:**
- Modify: `tesis/capitulos/06-diseno-metodologico.tex`
- Modify: `tesis/capitulos/07-desarrollo-implementacion.tex`
- Reference: `analisis/PROTOCOLO_PDR.md`
- Reference: `analisis/PROTOCOLO_PRUEBA_60MIN.md`
- Reference: `analisis/autonomia_bateria.py`
- Reference: firmware under `sensores espnow lora/`
- Test: `SmartSense_Trabajo_Grado.pdf`

**Interfaces:**
- Consumes: Concepts and cite keys from Task 3; evidence macros from Task 1.
- Produces: The methods and design basis required to interpret Task 5 results.

- [ ] **Step 1: Write the applied/experimental methodology and deployment protocol**

Describe requirements, component selection, prototype construction, functional verification, 60-minute representative test, 32-day deployment, range test, field temperature comparison and partial battery discharge. For every stage state input, procedure, recorded variable and output.

- [ ] **Step 2: Add the component-selection matrix**

Include PT100 versus DS18B20, E220 LoRa versus Wi-Fi/BLE, XIAO ESP32-C6, MAX31865, LiPo, microSD/LittleFS and the gateway display. Each row must contain requirement, selected component, engineering reason and acknowledged limitation.

- [ ] **Step 3: Add the 120-second sampling rationale**

Use this quantitative basis:

```latex
N_{día}=\frac{86\,400~\mathrm{s}}{120~\mathrm{s}}=720\ \text{muestras por nodo y día}.
```

Explain that thermal surface changes are slow relative to vibration, while the interval limits radio traffic, storage growth and energy consumption. State that 120 s is a design compromise validated for this pilot, not a universal optimum.

- [ ] **Step 4: Add the power and autonomy equations**

Include:

```latex
D=\frac{t_{activo}}{T}=\frac{2.279}{120}=0.01899\approx1.9\,\%.
```

Document the 1000 mAh test cell, 4.112 V to 4.089 V over 120 hours, linear lower-bound estimate and LiPo-curve model estimate. State explicitly that neither extrapolation demonstrates months of lifetime.

- [ ] **Step 5: Add the link evaluation equations**

Define:

```latex
\mathrm{PDR}=\frac{N_{recibidos}}{N_{enviados}}\times100\%,
\qquad
A_{datos}=\frac{N_{muestras\ disponibles}}{N_{muestras\ esperadas}}\times100\%.
```

Explain the interval inference `kT → k-1` missing frames, gateway-off exclusions, sequence-counter limitation, NLOS conditions, 433 MHz configuration and the 688 m lower bound.

- [ ] **Step 6: Write node, communication, gateway, storage and dashboard implementation sections**

Document the exact chain `PT100 → MAX31865 → ESP32-C6 → E220 → gateway → microSD/LittleFS → display/dashboard`, the three nodes, message fields, fixed addressing, 9600 bit/s UART, 2.4 kbit/s air rate, 30-day rolling storage and pre-rotation export of the analyzed dataset.

- [ ] **Step 7: Insert and cite the engineering figures**

Use `arquitectura-detalle.pdf`, `esquematico-nodo.pdf`, `esquematico-gateway.pdf`, `dashboard-principal.png`, `montaje-corriente.pdf` and the assembled-node evidence. Every caption must explain what the reader should learn and must not overstate validation.

- [ ] **Step 8: Compile and check figure/table references**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\compilar_trabajo_grado.ps1
rg -n "Reference.*undefined|There were undefined references|File .* not found|multiply defined" SmartSense_Trabajo_Grado.log
```

Expected: no missing figures and no unresolved references.

- [ ] **Step 9: Commit methodology and implementation**

```powershell
git add tesis/capitulos/06-diseno-metodologico.tex tesis/capitulos/07-desarrollo-implementacion.tex SmartSense_Trabajo_Grado.pdf
git commit -m "docs: documentar metodologia y diseño de SmartSense"
```

### Task 5: Write results, field evidence, discussion and conclusions

**Files:**
- Modify: `tesis/capitulos/08-resultados-evidencias.tex`
- Modify: `tesis/capitulos/09-discusion-limitaciones.tex`
- Modify: `tesis/capitulos/10-conclusiones.tex`
- Test: `SmartSense_Trabajo_Grado.pdf`

**Interfaces:**
- Consumes: Methods from Task 4 and all values/statuses from `tesis/datos-evidencia.tex`.
- Produces: The authoritative thesis interpretation reused during article correction.

- [ ] **Step 1: Write a claim-to-evidence results table**

Use the columns `Resultado`, `Método`, `Valor`, `Tipo de evidencia`, `Interpretación` and `Limitación`. Rows must cover node count, sample count, temperature range, field agreement, raw availability, active-gateway link availability, estimated PDR, 60-minute test, reset recovery, range/RSSI and autonomy estimate.

- [ ] **Step 2: Present the 32-day deployment without hiding gateway downtime**

Include the per-node statistics and both availability metrics. Explain that simultaneous outages support the gateway-off interpretation but do not prove that every non-simultaneous gap was a radio failure.

- [ ] **Step 3: Present field temperature agreement correctly**

Report five paired measurements over 65–71 °C, mean difference near 0.1 °C and maximum absolute difference 0.4 °C. Explain that the Fluke was used during offset adjustment, its uncertainty is ±1.5 °C, FLIR is ±2 °C, and a calibrated contact reference across multiple temperatures and nodes is required for absolute accuracy and combined uncertainty.

- [ ] **Step 4: Present packet delivery, range and autonomy with evidence labels**

Use “estimado por intervalos” for 32-day PDR, “recibidos frente a esperados” for the representative hour, “al menos 688 m” for range, and “autonomía estimada” for both discharge extrapolations.

- [ ] **Step 5: Present the suction-fan bearing case as an observation**

Order the narrative as temperature series → operational context → thermal image → later inspection association. State that the available evidence does not establish prediction, lead time, causality, sensitivity or false-alarm performance.

- [ ] **Step 6: Write limitations and threats to validity**

Include field-reference uncertainty, offset dependence, narrow temperature interval, one-node paired comparison, inferred PDR, moving range-test geometry, partial discharge, lack of synchronized latency, SNR not exposed, unverified web CSV export and incomplete PT100 manufacturer traceability if still unsupported.

- [ ] **Step 7: Write conclusions mapped to objectives**

Each conclusion must point to an objective and measured/inferred evidence. Future work must cover calibrated contact testing, current logging by state, direct sequence PDR, fixed-distance link characterization, synchronized latency and repository DOI publication.

- [ ] **Step 8: Compile and run the overclaim scan**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\compilar_trabajo_grado.ps1
rg -n -i "exactitud (de|es).*0[.,]4|autonomía (medida|demostrada).*177|alcance máximo.*688|detectó la falla|demuestra mantenimiento predictivo" tesis/capitulos
```

Expected: no unqualified overclaim matches.

- [ ] **Step 9: Commit results and conclusions**

```powershell
git add tesis/capitulos/08-resultados-evidencias.tex tesis/capitulos/09-discusion-limitaciones.tex tesis/capitulos/10-conclusiones.tex SmartSense_Trabajo_Grado.pdf
git commit -m "docs: consolidar resultados y limites de la tesis"
```

### Task 6: Add reproducibility annexes and defense traceability

**Files:**
- Modify: `tesis/anexos/anexos.tex`
- Modify: `SmartSense_Trabajo_Grado.tex`
- Test: `SmartSense_Trabajo_Grado.pdf`

**Interfaces:**
- Consumes: All completed thesis chapters and repository structure.
- Produces: A defense-question index and reproducibility map.

- [ ] **Step 1: Add the design-calculation annex**

Include duty cycle, expected samples/day, storage sizing method, battery extrapolation assumptions, PDR interval inference and the distinction between RSSI and SNR. Cross-reference the corresponding methodology and results sections.

- [ ] **Step 2: Add the reproducibility annex**

Map these resources: node/gateway firmware, schematics, CAD/STL enclosures, BOM, analysis scripts, figures, licenses and build scripts. State the real repository status and do not claim a Zenodo DOI until one exists.

- [ ] **Step 3: Add the defense matrix**

Create a `longtable` with columns `Pregunta`, `Respuesta breve`, `Evidencia` and `Sección`. Include every question supplied by the advisor, including accuracy, uncertainty, PDR, months-long autonomy, bearing evidence, 688 m interpretation, design calculations and component criteria.

- [ ] **Step 4: Compile and inspect table-of-content topology**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\compilar_trabajo_grado.ps1
rg -n "Introducción|Planteamiento del problema|Objetivos|Estado del arte|Marco de referencia|Diseño metodológico|Desarrollo e implementación|Resultados y evidencias|Discusión|Conclusiones|Información complementaria" SmartSense_Trabajo_Grado.toc
```

Expected: every major chapter and the annex entry appears once.

- [ ] **Step 5: Commit annexes**

```powershell
git add tesis/anexos/anexos.tex SmartSense_Trabajo_Grado.tex SmartSense_Trabajo_Grado.pdf
git commit -m "docs: agregar anexos y matriz de sustentacion"
```

### Task 7: Correct the HardwareX article against the thesis evidence

**Files:**
- Modify: `hardwarex/SmartSense_HardwareX.tex`
- Create: `hardwarex/compilar_hardwarex.ps1`
- Test: `hardwarex/SmartSense_HardwareX.pdf`

**Interfaces:**
- Consumes: Evidence wording and final interpretations from Tasks 1 and 5.
- Produces: Submission-ready HardwareX source and PDF consistent with the thesis.

- [ ] **Step 1: Tighten the title, abstract and main contribution**

Keep the title focused on an open-source wireless PT100 node and gateway for industrial surface-temperature monitoring. Replace any predictive-maintenance claim with `condition monitoring` or `support for condition-based maintenance`. Describe the deployment as a `32-day pilot deployment` or `long-duration field deployment`, not unqualified `long-term`.

- [ ] **Step 2: Replace the temperature-validation paragraphs**

Use the approved structure: purpose of field comparison, five simultaneous measurements, observed mean and maximum differences, FLIR corroboration, field-consistency interpretation, reference-instrument uncertainty, and calibrated-contact characterization as complementary future work. Do not call the Fluke comparison independent after stating that it was used for offset adjustment.

- [ ] **Step 3: Replace the battery-performance paragraphs**

State that a controlled five-day partial discharge under the nominal cycle supports a 177-day conservative linear estimate and a 291–307-day LiPo-curve estimate. Use `estimated autonomy`; explicitly say that these values are not experimentally demonstrated lifetime.

- [ ] **Step 4: Normalize the commercial comparison**

Update the table to include open hardware, PT100, communication, vibration, local storage, cloud dependency, IP rating, autonomy evidence type, software openness and cost basis. Replace `months (measured)` with `estimated from 5-day partial discharge` and retain WISE-2410 advantages.

- [ ] **Step 5: Correct link, storage, bearing and repository language**

Separate raw system availability from link availability during gateway-active periods; label PDR as inferred; state 688 m as a lower bound; explain the 30-day rolling store and pre-rotation export; describe the bearing case as a field observation; replace promised DOI wording with the repository's actual publication state.

- [ ] **Step 6: Add the article build script**

Use the same compiler search order as the thesis script, but set the working directory to `hardwarex`, source to `SmartSense_HardwareX.tex`, and expected PDF to `SmartSense_HardwareX.pdf`.

- [ ] **Step 7: Compile and scan the article**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\hardwarex\compilar_hardwarex.ps1
rg -n -i "predictive maintenance system|months \(measured\)|maximum range|absolute accuracy.*0[.,]4|experimentally demonstrated.*177" hardwarex/SmartSense_HardwareX.tex
rg -n "LaTeX Error|Undefined control sequence|File .* not found" hardwarex/SmartSense_HardwareX.log
```

Expected: both scans return no matches and the PDF is regenerated.

- [ ] **Step 8: Commit the corrected article**

```powershell
git add hardwarex/SmartSense_HardwareX.tex hardwarex/SmartSense_HardwareX.pdf hardwarex/compilar_hardwarex.ps1
git commit -m "docs: aplicar correcciones tecnicas al articulo HardwareX"
```

### Task 8: Final consistency audit, documentation and delivery PDFs

**Files:**
- Modify: `README.md`
- Modify: `COMPILAR_LATEX.md`
- Create or update: `PDF_Para_Compartir/SmartSense_Trabajo_Grado.pdf`
- Create or update: `PDF_Para_Compartir/SmartSense_HardwareX.pdf`
- Test: both source and delivery PDFs

**Interfaces:**
- Consumes: Final thesis and article sources/PDFs.
- Produces: Documented, reproducible delivery bundle.

- [ ] **Step 1: Update repository documentation**

Describe `SmartSense_Trabajo_Grado.tex` as the non-IEEE thesis, `SmartSense_IEEE.tex` as historical, the modular `tesis/` layout, article location, both build commands and final PDF paths.

- [ ] **Step 2: Compile both documents from a clean auxiliary-file state**

Remove only known generated auxiliaries for each master (`.aux`, `.bbl`, `.blg`, `.lof`, `.lot`, `.out`, `.toc`) after verifying their exact paths, then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\compilar_trabajo_grado.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\hardwarex\compilar_hardwarex.ps1
```

Expected: both commands exit 0 and both source PDFs have a current timestamp.

- [ ] **Step 3: Run cross-document numeric consistency checks**

Run:

```powershell
rg -n "51.?338|32.day|32 días|97.?8|98.?4|95.?3|96.?6|177|291|307|688|-98|2.?279" SmartSense_Trabajo_Grado.tex tesis hardwarex/SmartSense_HardwareX.tex
```

Expected: each value has the same meaning and evidence label in both deliverables.

- [ ] **Step 4: Run final structural and overclaim checks**

Run:

```powershell
rg -n "documentclass.*IEEEtran|begin\{IEEEkeywords\}|IEEEauthor" SmartSense_Trabajo_Grado.tex tesis
rg -n -i "autonomía (real|medida|demostrada).*mes|alcance máximo|detectó la falla|demuestra.*predict" SmartSense_Trabajo_Grado.tex tesis hardwarex/SmartSense_HardwareX.tex
rg -n "LaTeX Error|Undefined control sequence|Citation.*undefined|Reference.*undefined|File .* not found" SmartSense_Trabajo_Grado.log hardwarex/SmartSense_HardwareX.log
```

Expected: no matches.

- [ ] **Step 5: Copy final PDFs without losing previous user files**

If either target already exists, rename that exact target with a timestamp first. Then copy:

```powershell
Copy-Item -LiteralPath '.\SmartSense_Trabajo_Grado.pdf' -Destination '.\PDF_Para_Compartir\SmartSense_Trabajo_Grado.pdf'
Copy-Item -LiteralPath '.\hardwarex\SmartSense_HardwareX.pdf' -Destination '.\PDF_Para_Compartir\SmartSense_HardwareX.pdf'
```

- [ ] **Step 6: Verify PDF signatures and sizes**

Run:

```powershell
Get-Item '.\SmartSense_Trabajo_Grado.pdf', '.\hardwarex\SmartSense_HardwareX.pdf', '.\PDF_Para_Compartir\SmartSense_Trabajo_Grado.pdf', '.\PDF_Para_Compartir\SmartSense_HardwareX.pdf' | Select-Object FullName,Length,LastWriteTime
```

Expected: all four files exist, each length is greater than 100,000 bytes, and delivery copies match the source PDF sizes.

- [ ] **Step 7: Review repository status and commit only intended files**

Run:

```powershell
git status --short
git diff --check
```

Confirm that pre-existing unrelated files inside `PDF_Para_Compartir/` remain untouched. Then commit:

```powershell
git add README.md COMPILAR_LATEX.md PDF_Para_Compartir/SmartSense_Trabajo_Grado.pdf PDF_Para_Compartir/SmartSense_HardwareX.pdf
git commit -m "docs: preparar PDFs finales de tesis y articulo"
```

- [ ] **Step 8: Record final verification evidence**

Run:

```powershell
git status --short --branch
git log --oneline -8
```

Expected: only known pre-existing user files remain untracked or modified; the implementation commits for evidence, thesis, article and final delivery are present.
