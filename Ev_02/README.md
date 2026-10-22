# EV2 · Diseño de mockup de la app Android

**Estado:** sin iniciar · **Entrega:** alrededor de una semana después de la EV1 (fecha por confirmar).

> ⚠️ Según la actividad formativa TI3V42_U2_S3_AF, la modalidad es **individual**: cada integrante entrega su propio mockup. Confirmar con el docente.

## Qué hay que hacer (según TI3V42_U2_S3_AF)

1. **Planificación**
   - Concepto de la app en 2 o 3 líneas: qué problema resuelve y para qué sirve.
   - Público objetivo.
   - 3 o 4 funcionalidades principales.
2. **Arquitectura de pantallas**
   - Mapa de navegación entre pantallas.
   - Flujo de usuario, desde que abre la app hasta completar las tareas principales.
   - Lista de activities: **mínimo 3, máximo 5**.
3. **Mockup**
   - Herramientas sugeridas: Figma (recomendada), NinjaMock, Adobe XD, Sketch, Balsamiq, Draw.io o papel y lápiz.
   - Cada pantalla lleva header, contenido principal, navegación y footer si aplica.
   - Pantallas sugeridas: Splash, bienvenida u onboarding, Home, funcionalidades específicas y detalle.
4. **Documentación**
   - Por cada activity: nombre, propósito, elementos de interfaz, interacciones y navegación.
   - Funcionalidades técnicas: base de datos, conectividad, multimedia y sensores.
5. **Permisos**
   - Por cada permiso: cuál es, por qué se necesita, en qué activity se usa y cuándo se pide al usuario.

## Entregables

- **Mockup:** PDF o enlace a la herramienta online, con todas las pantallas identificadas.
- **Documento de especificaciones:** portada (nombre de la app, integrantes y fecha), descripción general, diagrama de flujo, detalle de las activities, tabla de permisos y tecnologías propuestas.

## Cómo se evalúa

Claridad de las pantallas · conexión correcta entre activities · permisos coherentes con lo que hace la app · presentación ordenada y creativa.

## Punto de partida sugerido

Ver la sección 5 de [`docs/CONTEXTO.md`](../docs/CONTEXTO.md): la app como panel de control del sistema de vibración de la EV1, reutilizando su protocolo, con permisos `INTERNET`, `POST_NOTIFICATIONS` y, si se usa Bluetooth, `BLUETOOTH_CONNECT`.
