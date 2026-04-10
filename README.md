# Lab 2 — Sistemas Embebidos | Universidad EIA

## Descripción

Laboratorio de programación sobre una matriz de LEDs bicolor 5×5 controlada por un ESP32 (ESP32-WROOM) usando el framework ESP-IDF en PlatformIO. El objetivo es implementar un videojuego en tiempo real manejando multiplexado de display, temporización con FreeRTOS y lectura de botones con debounce.

## Hardware

- **Microcontrolador:** ESP32-WROOM
- **Display:** Matriz LED bicolor 5×5 (verde y rojo), cátodo común por fila
- **Filas:** GPIO 13, 27, 33, 4, 15
- **Columnas verdes:** GPIO 14, 25, 23, 5, 2
- **Columnas rojas:** GPIO 12, 26, 32, 18, 0
- **Botones:** GPIO 19 (izquierda), 21 (disparo/acción), 22 (derecha)

## Juego — Dodger

El jugador (LED verde) se encuentra fijo en la última fila de la matriz y puede moverse horizontalmente con los botones. Obstáculos (LEDs rojos) caen desde la fila superior hacia abajo en columnas aleatorias. Si un obstáculo alcanza la misma posición del jugador, el juego termina con una animación de game over y se reinicia automáticamente. La velocidad de caída aumenta progresivamente cada 3 obstáculos esquivados.


