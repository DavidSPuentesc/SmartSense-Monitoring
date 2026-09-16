$ErrorActionPreference = 'Stop'

$articleRoot = $PSScriptRoot
$repositoryRoot = Split-Path -Parent $articleRoot
$source = Join-Path $articleRoot 'SmartSense_HardwareX.tex'
$expectedPdf = Join-Path $articleRoot 'SmartSense_HardwareX.pdf'
$localTectonic = Join-Path $repositoryRoot '.tools\tectonic-0.16.9\tectonic.exe'

if (-not (Test-Path -LiteralPath $source)) {
    throw 'No se encontro SmartSense_HardwareX.tex.'
}

Push-Location $articleRoot
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
        # References are an inline enumerate list, so this article needs no BibTeX pass.
        foreach ($pass in 1..3) {
            & pdflatex -interaction=nonstopmode -halt-on-error $source
            if ($LASTEXITCODE -ne 0) {
                throw "La compilacion $pass con pdflatex termino con codigo $LASTEXITCODE."
            }
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
