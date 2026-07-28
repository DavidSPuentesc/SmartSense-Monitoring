$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$source = Join-Path $root 'SmartSense_IEEE.tex'
$localTectonic = Join-Path $root '.tools\tectonic-0.16.9\tectonic.exe'

if (-not (Test-Path -LiteralPath $source)) {
    throw 'No se encontro SmartSense_IEEE.tex.'
}

Push-Location $root
try {
    if (Test-Path -LiteralPath $localTectonic) {
        & $localTectonic $source
    }
    elseif (Get-Command tectonic -ErrorAction SilentlyContinue) {
        & tectonic $source
    }
    elseif (Get-Command pdflatex -ErrorAction SilentlyContinue) {
        & pdflatex -interaction=nonstopmode -halt-on-error $source
        if ($LASTEXITCODE -eq 0) {
            & pdflatex -interaction=nonstopmode -halt-on-error $source
        }
    }
    else {
        throw 'Instala Tectonic o una distribucion LaTeX, o conserva el compilador en .tools\tectonic-0.16.9.'
    }

    if ($LASTEXITCODE -ne 0) {
        throw "La compilacion termino con codigo $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}

Write-Output (Join-Path $root 'SmartSense_IEEE.pdf')
