# Surgical De-Esser

De-esser split-band: reduce sibilancia ("eses") sin tocar el resto del
espectro. A diferencia de un de-esser "wideband" (que agacha toda la señal
cuando detecta una ese), este separa físicamente la señal en 3 bandas y
solo comprime la banda media.

## Cómo logra ser quirúrgico

1. **Crossover Linkwitz-Riley 4to orden** en `Inicio eses` y `Fin eses`
   (por defecto 3500 Hz y 9000 Hz). Es el mismo tipo de filtro que se usa
   para separar woofer/tweeter en monitores de estudio: al sumar las bandas
   de vuelta, la magnitud es perfectamente plana — no hay "agujeros" ni
   coloración por el simple hecho de dividir la señal.
2. Grave (< Inicio eses) y agudo (> Fin eses) **nunca se tocan** — pasan
   derecho, siempre, estén o no sonando una ese.
3. Solo la banda media (el rango típico de sibilancia) pasa por un
   compresor con Threshold/Ratio/Attack/Release. Cuando no hay ese, esa
   banda tampoco se toca (ganancia = 0 dB). Solo actúa cuando el nivel
   sube por encima del umbral.

## Parámetros

- **Inicio / Fin eses (Hz)**: define el rango de frecuencias tratado como
  sibilancia. Ajusta esto oyendo con **Listen** activado (ver abajo).
- **Threshold**: nivel a partir del cual empieza a actuar.
- **Ratio**: qué tan fuerte comprime una vez pasado el umbral.
- **Attack / Release**: velocidad de reacción del detector.
- **Max Cut (dB)**: límite duro de reducción, para que nunca "trague"
  demasiado la consonante aunque el threshold esté muy bajo.
- **Stereo Link**: detecta y aplica la misma reducción a ambos canales
  (recomendado para evitar que la imagen estéreo se mueva al des-esear).
- **Listen**: audiciona SOLO la banda de eses (aislada), muy útil para
  afinar Inicio/Fin eses hasta que oigas nada más que la "s" sin vocal.
- **Bypass**: apaga el proceso completo.

Hay un medidor de reducción de ganancia (GR) en la esquina superior derecha
que muestra en tiempo real cuánto se está cortando, en dB.

## Flujo de trabajo recomendado

1. Activa **Listen**.
2. Mueve **Inicio eses** y **Fin eses** hasta que escuches solo la sibilancia,
   sin cuerpo de voz ni aire de más.
3. Desactiva **Listen**.
3. Baja el **Threshold** hasta que el medidor de GR se mueva solo cuando
   suenan las eses (no todo el tiempo).
4. Ajusta **Ratio** y **Max Cut** al gusto — empieza suave (ratio 3-4,
   max cut 6-10 dB) y sube solo si sigue siendo audible.

## Compilar

```bash
git clone <tu-repo>
cd SurgicalDeEsser
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
El VST3 queda en `build/SurgicalDeEsser_artefacts/Release/VST3/`.

También puedes usar el workflow de GitHub Actions incluido en
`.github/workflows/build.yml` (compila automáticamente en cada push a `main`).

## Instalar en Studio One

Copia el `.vst3` a:
- Windows: `C:\Program Files\Common Files\VST3\`
- macOS: `/Library/Audio/Plug-Ins/VST3/`

Luego, en Studio One: **Opciones > Ubicaciones > VST Plug-Ins > Reescanear**.
