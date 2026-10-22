# Proyecto IoT · Monitoreo de vibración con activación de alarma

Aplicaciones Móviles para IoT (TI3V42) · INACAP Apoquindo · P14
**Equipo:** Walther Mora · Maximiliano Soto

Dos ESP32, simulados en [Wokwi](https://wokwi.com), se comunican por **WiFi + MQTT**:

- El **nodo sensor** mide la vibración con un acelerómetro MPU6050, distingue un golpe aislado de una vibración sostenida y activa una alarma.
- El **nodo de control** muestra las lecturas y permite cambiar el umbral y encender o apagar la alarma a distancia.

Los mensajes van firmados (HMAC-SHA256) y validados.

## Estado

| Entrega | Qué es | Estado |
|---|---|---|
| **EV1** · Prototipo Arduino con conexión inalámbrica (20 %, dupla) | Código de los dos nodos + README técnico | ✅ Lista para entregar el 26-10-2026; faltan el ensayo de punta a punta y subirla a Moodle |
| **EV2** · Diseño de mockup | Mockup de la app Android + especificaciones | ⏳ Sin iniciar; fecha por confirmar |

## Estructura

```
├── docs/CONTEXTO.md               ← empezar aquí: curso, rúbrica, decisiones y pendientes
├── Ev_01/
│   ├── entrega/EVA1_Vibracion/    ← lo que se entrega en la EV1 (ver su README)
│   └── aporte_colaborador/        ← primer avance (UNO + HC-05), solo como referencia
├── Ev_02/README.md                ← qué pide la EV2 y punto de partida
└── contexto/README.md             ← índice de los PDF del curso (no se suben)
```

## Inicio rápido

- **Entender el proyecto:** [`docs/CONTEXTO.md`](docs/CONTEXTO.md).
- **Ver o probar la EV1:** [`Ev_01/entrega/EVA1_Vibracion/README.md`](Ev_01/entrega/EVA1_Vibracion/README.md). La sección 8 explica cómo correrla en Wokwi.
