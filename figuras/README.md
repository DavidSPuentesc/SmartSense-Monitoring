# Figuras requeridas para el artículo IEEE

Guarda las imágenes preferiblemente en formato PNG o PDF y con al menos 1200 píxeles de ancho.

Nombres sugeridos:

- `arquitectura.png`: diagrama del sistema completo.
- `dashboard-general.png`: dashboard con los nodos conectados.
- `historico-succionador.png`: gráfica del día donde aumentó la temperatura.
- `instalacion-nodo-1.jpg`: PT100 instalada sobre el primer equipo.
- `comparacion-fluke.jpg`: medición simultánea con la pistola Fluke.
- `camara-termografica.jpg`: captura de la cámara usada para ajustar el offset.
- `wise-2410.png`: interfaz del WISE-2410 utilizada en planta.
- `rodamientos.jpg`: evidencia de inspección o mantenimiento, si puede publicarse.

Actualmente el archivo `SmartSense_IEEE.tex` contiene recuadros provisionales. Cuando estén disponibles las imágenes se sustituyen por instrucciones como:

```latex
\begin{figure}[!t]
  \centering
  \includegraphics[width=\columnwidth]{historico-succionador.png}
  \caption{Histórico térmico del motor del succionador.}
  \label{fig:historico-succionador}
\end{figure}
```
