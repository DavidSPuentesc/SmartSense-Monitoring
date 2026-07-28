# SmartSense Monitoring

Proyecto académico de monitoreo industrial de temperatura mediante sensores
PT100 de tres hilos, comunicación LoRa y visualización histórica en un
dashboard local.

## Contenido del repositorio

- `SmartSense_IEEE.tex`: documento principal en formato IEEE.
- `figuras/`: imágenes utilizadas por el documento LaTeX.
- `figuras_anteproyecto/`: diagramas del anteproyecto.
- `Entrega_SmartSense/`: última versión de los dos documentos de entrega.
- `compilar_ieee.ps1`: compilación local en Windows.
- `COMPILAR_LATEX.md`: instrucciones de compilación.

La carpeta `.tools` no se sube porque contiene un ejecutable local. Cada
integrante puede instalar Tectonic o una distribución de LaTeX en su equipo.

## Trabajo en paralelo

Cada integrante debe crear una rama para sus cambios:

```powershell
git switch main
git pull
git switch -c nombre/cambio
```

Después de editar:

```powershell
git add .
git commit -m "Describe brevemente el cambio"
git push -u origin nombre/cambio
```

En GitHub se abre un *pull request* hacia `main`. No se recomienda que dos
personas editen simultáneamente la misma sección del archivo `.tex`; conviene
repartir capítulos o figuras y combinar los cambios mediante el *pull request*.

## Compilación

En Windows:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\compilar_ieee.ps1
```

El PDF generado en la raíz es un archivo de trabajo y no se versiona. Cuando
se apruebe una nueva entrega, se reemplaza manualmente el PDF dentro de
`Entrega_SmartSense`.

