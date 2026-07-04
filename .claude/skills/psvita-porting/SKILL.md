---
name: psvita-porting
description: Guía avanzada para el desarrollo, control de calidad, linting, depuración y optimización de ports de Android a PS Vita.
---

# Directrices de Desarrollo Robustas para Ports de PS Vita

Este conjunto de directrices sirve como referencia técnica para mantener el código limpio, optimizar el rendimiento de la consola, depurar errores de sistema e implementar pruebas estructurales en puertos basados en SoLoader.

## Contenido de la Habilidad

- **Clean Code y Buenas Prácticas en C/C++**: [references/clean_code.md](references/clean_code.md)
  - Gestión de memoria segura, prevención de fugas y convención de nombres.
- **Depuración de Errores y Coredumps (C2-12828-1)**: [references/debugging.md](references/debugging.md)
  - Cómo leer volcados de memoria, usar `vita-parse-core` y logs del sistema.
- **Optimización y Prevención de Bugs Gráficos**: [references/optimization.md](references/optimization.md)
  - Control de shaders, limpieza de caché de instrucciones y gestión del heap de la GPU.
- **Linting, Verificación Estática y Pruebas**: [references/lint_testing.md](references/lint_testing.md)
  - Validación de estructuras JNI simuladas y automatización de validaciones.
