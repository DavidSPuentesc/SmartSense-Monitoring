# Compilar la tesis y el artículo HardwareX

## Compilación en Windows

Cada script busca primero `.tools/tectonic-0.16.9/tectonic.exe` en la raíz,
luego `tectonic` en PATH y finalmente `pdflatex` en PATH. La ruta Tectonic
0.16.9 es la verificada para esta entrega; puede necesitar Internet para
descargar paquetes a su caché.

Desde la raíz del repositorio, ejecutar:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\compilar_trabajo_grado.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\hardwarex\compilar_hardwarex.ps1
```

La carpeta `.tools` está excluida de Git porque contiene un ejecutable local.
Cada integrante necesita Tectonic o una distribución de LaTeX.

La tesis usa `report`, `tesis/`, `figuras/` y bibliografía BibTeX `apalike`.
Su alternativa ejecuta pdfLaTeX, BibTeX y dos pasadas más de pdfLaTeX. El
artículo se compila desde `hardwarex/`, referencia `../figuras/` y tiene
bibliografía integrada: su alternativa usa tres pasadas de pdfLaTeX, sin
BibTeX. Las alternativas no se ejecutaron en esta verificación.

| Fuente | PDF y log de trabajo | Copia de entrega |
|---|---|---|
| `SmartSense_Trabajo_Grado.tex` | `SmartSense_Trabajo_Grado.pdf` / `.log` en raíz | `PDF_Para_Compartir/SmartSense_Trabajo_Grado.pdf` |
| `hardwarex/SmartSense_HardwareX.tex` | `hardwarex/SmartSense_HardwareX.pdf` / `.log` | `PDF_Para_Compartir/SmartSense_HardwareX.pdf` |

Los scripts fallan si el compilador devuelve error o falta el PDF esperado.
Comprobar también que su fecha de modificación sea posterior al inicio de
compilación. El IEEE histórico conserva `compilar_ieee.ps1`.

## Reconstrucción desde auxiliares limpios

Para una entrega, este bloque elimina solo auxiliares conocidos de los dos
maestros tras comprobar su ubicación. No elimina fuentes, figuras ni PDF.

```powershell
$buildRoot = (Get-Location).ProviderPath
if (-not (Test-Path -LiteralPath (Join-Path $buildRoot 'SmartSense_Trabajo_Grado.tex'))) {
    throw 'Ejecutar desde la raíz del repositorio.'
}
$masters = @('SmartSense_Trabajo_Grado', 'hardwarex/SmartSense_HardwareX')
$extensions = @('.aux', '.bbl', '.blg', '.lof', '.lot', '.out', '.toc')
foreach ($master in $masters) {
    foreach ($extension in $extensions) {
        $auxPath = [IO.Path]::GetFullPath((Join-Path $buildRoot ($master + $extension)))
        if (-not $auxPath.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Auxiliar fuera del repositorio: $auxPath"
        }
        if (Test-Path -LiteralPath $auxPath) { Remove-Item -LiteralPath $auxPath }
    }
}
```

Después ejecutar ambos scripts del primer bloque. Tectonic conserva logs
con `--keep-logs`; para revisar el `.toc` puede ejecutarse además con
`--keep-intermediates` desde la carpeta del maestro.

## Verificación y copia de entrega

Revisar ambos PDF y buscar errores o referencias sin resolver:

```powershell
rg -n 'LaTeX Error|Undefined control sequence|Citation.*undefined|Reference.*undefined|File .* not found|Overfull|Missing character' SmartSense_Trabajo_Grado.log hardwarex/SmartSense_HardwareX.log
git diff --check
Get-Item SmartSense_Trabajo_Grado.pdf, hardwarex/SmartSense_HardwareX.pdf | Select-Object Name,Length,LastWriteTime
```

La búsqueda debe terminar sin coincidencias (`rg` devuelve 1 en ese caso).
Los avisos de `inputenc` bajo XeTeX y el diagnóstico Fontconfig del entorno
Tectonic no impidieron generar los documentos. Una compilación correcta no
sustituye revisar tablas, figuras, índices y enlaces del PDF.

Se encontró PyMuPDF **1.28.2** instalado en el Python del usuario y se utilizó
para inspección, sin desinstalar ni añadir dependencias. Verificar con
`python -c "import pymupdf; print(pymupdf.VersionBind)"`. Es opcional y no se
necesita para compilar. Si otro equipo requiere herramientas de revisión,
instalarlas en un entorno virtual separado.

Conservar cualquier destino existente antes de copiar. Este bloque respalda
únicamente los dos nombres de entrega actuales:

```powershell
$deliveryRoot = Join-Path (Get-Location).ProviderPath 'PDF_Para_Compartir'
New-Item -ItemType Directory -Path $deliveryRoot -Force | Out-Null
$deliveries = @(
    @('SmartSense_Trabajo_Grado.pdf', 'SmartSense_Trabajo_Grado.pdf'),
    @('hardwarex/SmartSense_HardwareX.pdf', 'SmartSense_HardwareX.pdf')
)
foreach ($entry in $deliveries) {
    $destination = Join-Path $deliveryRoot $entry[1]
    if (Test-Path -LiteralPath $destination) {
        $backupName = [IO.Path]::GetFileNameWithoutExtension($entry[1]) + '.previo-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.pdf'
        Rename-Item -LiteralPath $destination -NewName $backupName
    }
    Copy-Item -LiteralPath $entry[0] -Destination $destination
    if ((Get-FileHash -LiteralPath $entry[0]).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) {
        throw "La copia no coincide: $destination"
    }
}
```

Mantener intactos los PDF antiguos `Articulo_SmartSense_HardwareX.pdf` y
`Tesis_SmartSense_Monitoring.pdf`: registrar SHA-256 antes y después de la
entrega. Versionar solo los archivos previstos, sin añadir auxiliares,
herramientas ni archivos anteriores del usuario por accidente. Compilar no
publica el repositorio ni crea un DOI.
