# Task 1 report: shared evidence ledger

## Implementation

Created `tesis/datos-evidencia.tex` as the authoritative quantitative evidence interface. It defines all accepted numeric macros from the brief, plus the four evidence-status wording constants. The file is UTF-8 encoded and explicitly warns against redefining values in thesis chapters.

## Verification commands and outputs

Ran the required source traceability search:

```powershell
rg -n "51338|51\.338|32 días|121|97\.8|98\.4|95\.3|96\.6|3\.45|4\.68|0\.4|0\.1|177|291|307|688|-98|2\.279|1\.9" SmartSense_IEEE.tex hardwarex/SmartSense_HardwareX.tex analisis
```

Result: matches were found in `SmartSense_IEEE.tex`, `hardwarex/SmartSense_HardwareX.tex`, and the relevant `analisis` scripts, including the deployment totals, cadence, availability/PDR, thermal comparison, autonomy, range/RSSI, active time, and duty cycle.

Ran the overclaim check:

```powershell
rg -n -i "exactitud.*0[.,]4|autonomía medida.*177|alcance máximo|detección predictiva" tesis/datos-evidencia.tex
```

Result: no matches.

Ran whitespace validation:

```powershell
git diff --check
```

Result: no output (no whitespace errors).

## Files changed

- `tesis/datos-evidencia.tex` (created)
- This report file (created for the task handoff; not included in the implementation commit)

The pre-existing untracked `PDF_Para_Compartir/` directory was not touched or staged.

## Self-review

- All 17 numeric macros and all four status constants from the brief are present with the accepted spellings and values.
- The ledger uses ranges with LaTeX `--`, thin spaces before percentages, and nonbreaking spaces before units as specified.
- Status wording preserves the required limits: field concordance is not metrological calibration; autonomy is based on a partial five-day discharge; PDR is inferred from active-period arrivals; range is a lower bound while the link remained operational.
- No thesis/article files were modified.

## Concerns

The HardwareX text contains an approximate autonomy range of 290--307 days, while the accepted ledger value is 291--307 days and the thesis contains the accepted 291--307 range. The ledger follows the brief and the thesis-authoritative value; this should remain an explicit consistency decision in later article checks.

## Fix round 1

The autonomy discrepancy was resolved by tracing the accepted lower endpoint to the generating analysis script rather than averaging source prose. `analisis/autonomia_bateria.py` computes the LiPo model from the measured points (4.112 V at 0 h and 4.089 V at 120 h), interpolates the OCV--SoC table, and prints 291 days to the 3.30 V cutoff and 307 days to the 3.00 V cutoff. The ledger now records this derivation and source-of-truth decision in comments. HardwareX was not modified; its rounded 290--307-day prose remains owned by Task 7.

Commands and outputs for the fix:

```powershell
python analisis/autonomia_bateria.py
```

```text
Tasa de caida: 0.192 mV/h
Autonomia hasta 3.30 V: 4237 h = 177 dias = 5.9 meses
Autonomia hasta 3.00 V: 5802 h = 242 dias = 8.1 meses

Corriente promedio (a 3.30 V): 0.236 mA
Ciclo de trabajo: 1.90 %
Corriente activa implicada (promedio del pico): 11.7 mA

[Modelo LiPo] SoC 94.1% -> 92.6% | corriente 0.128 mA
[Modelo LiPo] Autonomia hasta 3.30 V: 6976 h = 291 dias = 9.7 meses
[Modelo LiPo] Autonomia hasta 3.00 V: 7367 h = 307 dias = 10.2 meses
OK autonomia-descarga
OK autonomia-ciclo
```

```powershell
rg -n "291|290|307|3\.30|3\.00|V0|V5|H" analisis/autonomia_bateria.py tesis/datos-evidencia.tex SmartSense_IEEE.tex hardwarex/SmartSense_HardwareX.tex
```

Result: the generator contains the measured inputs, cutoff values, and calculated 291/307-day outputs; the ledger documents and adopts those outputs. The thesis contains 291--307 days; HardwareX retains approximate 290--307 days pending Task 7.

```powershell
rg -n -i "exactitud.*0[.,]4|autonomía medida.*177|alcance máximo|detección predictiva" tesis/datos-evidencia.tex
```

Result: no matches.

```powershell
git diff --check
```

Result: no output (no whitespace errors).
