# Contexto del proyecto

Documento de traspaso para quien continúe el trabajo. Resume qué pide el curso, qué se decidió y por qué, en qué estado está cada entrega y qué falta. Fecha de corte: **21-10-2026**.

## 1. El curso

- **Asignatura:** Aplicaciones Móviles para IoT, TI3V42 (la guía de la Unidad 2 la llama TI3042), INACAP Apoquindo, sección P14.
- **Enfoque del docente:** curso primariamente teórico. Se trabaja con **simuladores** y comprar placas reales es opcional. **El docente aceptó Wokwi y el uso de un broker MQTT público** para la EV1.
- **Equipo:** Walther Mora y Maximiliano Soto, ambos colaboradores.

## 2. Documentos fuente

Están en el Aula Virtual (AAI) y en la carpeta local `contexto/`. No se suben al repositorio porque es público y son material de INACAP. Ver [`contexto/README.md`](../contexto/README.md).

| Documento | De qué trata |
|---|---|
| Enunciado EVA1 | Qué construir, qué entregar y la rúbrica de 30 puntos |
| Guía EVA1 | Los criterios explicados con analogías, más un tutorial completo (andén de carga con UNO + HC-05) |
| TI3V42_U1_S1_AF | Actividad formativa de la Unidad 1: los 6 casos para elegir |
| TI3V42_U1_S1_LEC | Lectura: IoT, estándares OT (IEC 62443, NIST 800-82), comunicación serial y Arduino |
| TI3V42_U2_S3_AF | Actividad formativa de la Unidad 2: diseño del mockup de la app Android |
| TI3V42_U2_S3_LEC | Lectura: ecosistema Android, activities, permisos, Bluetooth e ISO/IEC 27400 |
| Guía de desarrollo Unidad 2 | Paso a paso de la app Android (Kotlin + Compose) en 9 hitos, formativa y sumativa |

## 3. Caso elegido

**Monitoreo de vibración con activación de alarma** (uno de los 6 casos de la formativa de la Unidad 1). Sistema de integridad estructural que detecta vibraciones anómalas en maquinaria o edificaciones. Todos los casos exigen dos funciones:

1. **Umbral de activación configurable** a distancia.
2. **Control manual remoto** del actuador, en este caso la alarma.

## 4. EV1 · Prototipo Arduino con conexión inalámbrica

**Peso:** 20 % · en dupla · rúbrica de 30 puntos · **entrega: lunes 26-10-2026** (la fecha la confirma el docente en clase y queda en SIGA).

### Qué pide el enunciado

- **Nodo sensor (microcontrolador 1):**
  - Lee el sensor periódicamente sin esperas largas.
  - Activa el actuador al superar el umbral.
  - Envía lecturas y estado por Bluetooth o WiFi con un formato propio.
  - Valida y confirma los comandos que recibe.
- **Nodo de control (microcontrolador 2):** recibe, interpreta y muestra las lecturas, y envía comandos para cambiar el umbral y encender o apagar el actuador.
- **Entrega:**
  - Los dos sketches `.ino`, que deben compilar en Arduino IDE.
  - Un README con: integrantes y caso, tabla de componentes, diagrama de conexión, formato del mensaje, brechas de seguridad con sus medidas y prácticas OT.
  - **Cada integrante sube el mismo archivo a Moodle.**

### Rúbrica en resumen (5 puntos por indicador para "Logrado")

| Indicador | Para 5 puntos |
|---|---|
| 1.1.1 Componentes | Tabla de los 2 microcontroladores, el sensor, el actuador y el módulo inalámbrico, con voltaje, interfaz y rango, y justificación de cada uno |
| 1.1.2 Brechas | 3 o más brechas propias del prototipo, con cómo se aprovechan y qué consecuencia tienen |
| 1.1.2 Medidas | 2 o más medidas en el código; un mensaje inválido se rechaza sin ejecutar nada |
| 1.1.3 Prácticas OT | Formato documentado, lecturas sin bloqueo, modos AUTO/MAN, estado seguro al perder la comunicación y relación con ISA/IEC 62443 |
| 1.1.4 Interconexión | Envío inalámbrico; el segundo nodo recibe, interpreta y muestra; ambos códigos compilan y funcionan |
| 1.1.4 Control remoto | Cambiar el umbral y encender o apagar a distancia, con confirmación de cada comando, y activación automática por umbral |

### Decisiones tomadas y por qué

| Decisión | Motivo |
|---|---|
| **Solo simulación**, sin comprar placas | El tiempo es el factor clave. |
| **Wokwi** en vez de Tinkercad | Tinkercad no tiene placas ni módulos con WiFi o Bluetooth, así que solo permitiría unir los nodos por cable, y la rúbrica califica eso como "Por lograr". El docente aceptó Wokwi. |
| **2× ESP32 + WiFi + MQTT** (`broker.hivemq.com`) en vez de UNO + HC-05 | Wokwi no simula Bluetooth entre dos placas. MQTT es el estándar IoT que cita la lectura de la Unidad 1. El docente aceptó el broker. |
| **MPU6050** en vez de SW-420 | Wokwi no tiene SW-420, y la guía acepta "SW-420 o acelerómetro". Con el acelerómetro el umbral tiene unidades reales (mg). |
| **Firma HMAC-SHA256 + número de secuencia** | El broker es público: sin firma, cualquiera podría publicar `M,0` y apagar la alarma. |
| Se mantuvo el algoritmo N/T/A/F del primer avance de Max | Distingue un golpe aislado (T) de una vibración sostenida (A) y tiene histéresis. Es el aporte propio frente al tutorial de la guía. |

### Estado al 21-10-2026

- Código y README completos en [`Ev_01/entrega/EVA1_Vibracion/`](../Ev_01/entrega/EVA1_Vibracion/).
- **Compila** para ESP32 Dev Module (núcleo esp32 3.3.12, PubSubClient 2.8) sin errores ni advertencias.
- **15 pruebas aprobadas** en Wokwi; están documentadas en la sección 9 del README de la entrega. Incluyen un ataque automatizado con `herramientas/probar_seguridad.py`, que dio 9/9.
- Diagramas rotulados: `diagrama_sensor.png` y `diagrama_control.png`.
- El primer avance (Arduino UNO + HC-05) queda como referencia en [`Ev_01/aporte_colaborador/`](../Ev_01/aporte_colaborador/). **No es lo que se entrega.**

### Pendiente para la EV1

- [ ] **Ensayo de punta a punta:** los dos nodos corriendo a la vez en Wokwi, en **dos ventanas** del navegador lado a lado. Wokwi frena las pestañas que no están a la vista. Cada nodo se probó por separado contra Python; los dos juntos todavía no.
- [ ] Probar el estado `F` (falla del sensor) quitando el MPU6050 del `diagram.json`.
- [ ] (Opcional) Guardar ambos proyectos en una cuenta de Wokwi para tener enlaces fijos para la demostración.
- [ ] Armar el `.zip` (o enviar el enlace al repositorio) y que **ambos integrantes** lo suban a Moodle.
- [ ] Ambos deben poder explicar la firma HMAC y el número de secuencia: son lo que más se aleja de la guía.

## 5. EV2 · Diseño de mockup

**Entrega:** alrededor de una semana después de la EV1 (fecha por confirmar).

> ⚠️ **La actividad del mockup es INDIVIDUAL** según el documento TI3V42_U2_S3_AF. Cada integrante debe entregar el suyo. Confirmar con el docente si en esta sección se acepta en dupla.

Detalle de lo que se pide en [`Ev_02/README.md`](../Ev_02/README.md). En resumen:

- Concepto de la app en 2 o 3 líneas, público objetivo y 3 o 4 funcionalidades.
- Mapa de navegación y flujo de usuario; **entre 3 y 5 activities**.
- Mockup en Figma (recomendado), NinjaMock, Draw.io u otra herramienta, con header, contenido, navegación y footer.
- Ficha por activity: nombre, propósito, elementos, interacciones y navegación.
- Funcionalidades técnicas: datos que guarda, conectividad, multimedia y sensores.
- Tabla de **permisos**: cuál, por qué, en qué activity y cuándo se pide.
- **Entregable:** PDF o enlace con el mockup, más un documento de especificaciones (portada, descripción, diagrama de flujo, activities, permisos y tecnologías).
- **Se evalúa:** claridad de las pantallas, conexión correcta entre activities, permisos coherentes y presentación.

### Ideas que ya calzan con la EV1

- La app sería el **panel de control** del sistema de vibración. Muestra intensidad, estado (N/T/A/F), alarma y modo; permite cambiar el umbral y encender o apagar la alarma; avisa con una notificación cuando el estado pasa a ANÓMALA.
- El **protocolo de la EV1 se reutiliza tal cual**: tramas `$V,…` y comandos `#seq,…`, con checksum y firma. La app sería otro suscriptor MQTT.
- **Permisos probables:**
  - `INTERNET`, para MQTT o WiFi.
  - `POST_NOTIFICATIONS`, para la alerta (Android 13 o superior).
  - `BLUETOOTH_CONNECT`, si en la Unidad 2 se usa Bluetooth entre teléfonos como propone la guía.
- La **guía de la Unidad 2** recomienda Android Studio + Kotlin + Jetpack Compose, Bluetooth clásico (o WiFi TCP), SQLite, PBKDF2 y AES-GCM, y notificaciones locales. Su mockup de ejemplo tiene las pantallas Login → Conexión → Nodo/Panel → Historial.
- **Aviso para más adelante:** la sumativa de la Unidad 2 (25 %) exige **dos teléfonos Android reales** en la demostración; el emulador no basta.

## 6. Cómo trabajar en este repositorio

- No subir los PDF del curso ni claves reales. La clave HMAC del código es de demostración.
- Probar en Wokwi siguiendo la sección 8 del README de la entrega.
- **Compilar sin abrir la IDE:**
  ```
  arduino-cli compile --fqbn esp32:esp32:esp32 Ev_01/entrega/EVA1_Vibracion/nodo_sensor
  ```
  Requiere el núcleo esp32 y la librería PubSubClient.
- Si se cambia el código, volver a correr `python Ev_01/entrega/EVA1_Vibracion/herramientas/probar_seguridad.py` con el sensor simulado en marcha.
