$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$source = Join-Path $root 'SmartSense_Trabajo_Grado.tex'
$expectedPdf = Join-Path $root 'SmartSense_Trabajo_Grado.pdf'
$localTectonic = Join-Path $root '.tools\tectonic-0.16.9\tectonic.exe'

if (-not (Test-Path -LiteralPath $source)) {
    throw 'No se encontro SmartSense_Trabajo_Grado.tex.'
}

Push-Location $root
try {
    if (Test-Path -LiteralPath $localTectonic) {
        & $localTectonic --keep-logs $source
        if ($LASTEXITCODE -ne 0) {
            throw "La compilacion con Tectonic termino con codigo $LASTEXITCODE."
        }
    }
    elseif (Get-Command tectonic -ErrorAction SilentlyContinue) {
        & tectonic --keep-logs $source
        if ($LASTEXITCODE -ne 0) {
            throw "La compilacion con Tectonic termino con codigo $LASTEXITCODE."
        }
    }
    elseif (Get-Command pdflatex -ErrorAction SilentlyContinue) {
        & pdflatex -interaction=nonstopmode -halt-on-error $source
        if ($LASTEXITCODE -ne 0) {
            throw "La primera compilacion con pdflatex termino con codigo $LASTEXITCODE."
        }
        & bibtex SmartSense_Trabajo_Grado
        if ($LASTEXITCODE -ne 0) {
            throw "La compilacion de bibliografia termino con codigo $LASTEXITCODE."
        }
        & pdflatex -interaction=nonstopmode -halt-on-error $source
        if ($LASTEXITCODE -ne 0) {
            throw "La segunda compilacion con pdflatex termino con codigo $LASTEXITCODE."
        }
        & pdflatex -interaction=nonstopmode -halt-on-error $source
        if ($LASTEXITCODE -ne 0) {
            throw "La tercera compilacion con pdflatex termino con codigo $LASTEXITCODE."
        }
    }
    else {
        throw 'Instala Tectonic o una distribucion LaTeX, o conserva el compilador en .tools\tectonic-0.16.9.'
    }

    if (-not (Test-Path -LiteralPath $expectedPdf)) {
        throw "No se genero el PDF esperado: $expectedPdf"
    }
}
finally {
    Pop-Location
}

Write-Output $expectedPdf
