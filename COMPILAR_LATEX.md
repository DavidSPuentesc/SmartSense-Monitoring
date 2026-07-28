# Cómo compilar el artículo

## Compilación en Windows

El script busca, en este orden, el compilador portátil ubicado en
`.tools/tectonic-0.16.9`, una instalación de Tectonic disponible en el sistema
o una instalación de `pdflatex`.

Para regenerar el PDF después de cambiar el texto o agregar figuras, ejecuta:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\compilar_ieee.ps1
```

La carpeta `.tools` está excluida de Git porque contiene un ejecutable local.
El otro integrante debe instalar Tectonic o una distribución de LaTeX, o usar
Overleaf.

## Alternativa: Overleaf

1. Crea un proyecto en blanco en Overleaf.
2. Sube `SmartSense_IEEE.tex`.
3. Crea la carpeta `figuras` y sube allí las evidencias.
4. Selecciona pdfLaTeX como compilador.
5. Compila el proyecto.

El archivo utiliza la clase `IEEEtran`, incluida normalmente en Overleaf.

## Compilación directa con otra distribución TeX

Ejecuta dos veces:

```powershell
pdflatex SmartSense_IEEE.tex
pdflatex SmartSense_IEEE.tex
```

El artículo fue validado con Tectonic 0.16.9.

