// ============================================================================
// EVA1 - Aplicaciones Moviles para IoT (TI3V42) - INACAP Apoquindo
// Caso: Monitoreo de vibracion con activacion de alarma
//
// NODO DE CONTROL (microcontrolador 2) - Arduino UNO + HC-05 (maestro)
//
// Que hace:
//  - Recibe las tramas del nodo sensor, valida su checksum, las interpreta
//    y las muestra en el Monitor Serie en lenguaje claro.
//  - Traduce lo que escribe el operador a comandos del protocolo:
//        u 30   -> cambia el umbral a 30 eventos/s
//        on     -> alarma manual encendida
//        off    -> alarma manual apagada
//        auto   -> vuelve al modo automatico
//  - Envia un latido cada 2 s para que el sensor sepa que el enlace sigue vivo.
//  - LED 13: se enciende si deja de recibir lecturas (enlace caido).
//  - LED 12: se enciende cuando el sensor reporta la alarma activa.
// ============================================================================

#include <SoftwareSerial.h>

const byte BT_RX = 10;            // va al TX del HC-05
const byte BT_TX = 11;            // va al RX del HC-05, con divisor 1k/2k
const byte PIN_LED_ENLACE = 13;   // enlace caido
const byte PIN_LED_ALARMA = 12;   // repetidor de la alarma remota

SoftwareSerial bt(BT_RX, BT_TX);

const unsigned long PERIODO_LATIDO   = 2000;  // ms entre latidos
const unsigned long LIMITE_SIN_DATOS = 3000;  // ms sin lecturas del sensor

unsigned long ultimoLatido = 0;
unsigned long ultimoDato = 0;
bool sinEnlace = false;

char trama[40];
byte largoTrama = 0;
bool recibiendo = false;

char orden[24];
byte largoOrden = 0;

// Mismo checksum XOR que el nodo sensor.
byte checksum(const char *texto) {
  byte cs = 0;
  for (const char *p = texto; *p; p++) cs ^= *p;
  return cs;
}

void enviarComando(const char *contenido) {
  char salida[32];
  snprintf(salida, sizeof(salida), "#%s*%02X", contenido, checksum(contenido));
  bt.println(salida);
}

// Traduce la letra de estado a texto.
const __FlashStringHelper *textoEstado(char e) {
  switch (e) {
    case 'N': return F("NORMAL");
    case 'T': return F("TRANSITORIA (golpe aislado)");
    case 'A': return F("ANOMALA");
    case 'F': return F("FALLA DEL SENSOR");
    default:  return F("?");
  }
}

// Interpreta "eventos,estado,alarma,modo,umbral" y lo muestra.
void mostrarLectura(const char *datos) {
  unsigned int eventos;
  char estado, modo;
  int alarma, umbral;
  if (sscanf(datos, "%u,%c,%d,%c,%d", &eventos, &estado, &alarma, &modo, &umbral) != 5) {
    Serial.println(F("Lectura con formato raro, descartada"));
    return;
  }
  digitalWrite(PIN_LED_ALARMA, alarma ? HIGH : LOW);

  Serial.print(F("Vibracion: "));
  Serial.print(eventos);
  Serial.print(F(" ev/s (umbral "));
  Serial.print(umbral);
  Serial.print(F(") | Estado: "));
  Serial.print(textoEstado(estado));
  Serial.print(alarma ? F(" | Alarma: ENCENDIDA") : F(" | Alarma: apagada"));
  Serial.println(modo == 'M' ? F(" | Modo: MANUAL") : F(" | Modo: AUTO"));
}

void procesarTrama(char *linea) {
  char *asterisco = strchr(linea, '*');
  if (asterisco == NULL) { Serial.println(F("Trama sin checksum, descartada")); return; }
  *asterisco = '\0';
  const char *recibido = asterisco + 1;
  if (strlen(recibido) != 2 || (byte)strtol(recibido, NULL, 16) != checksum(linea)) {
    Serial.println(F("Checksum incorrecto, trama descartada"));
    return;
  }

  ultimoDato = millis();
  if (sinEnlace) {
    sinEnlace = false;
    digitalWrite(PIN_LED_ENLACE, LOW);
    Serial.println(F("Enlace recuperado"));
  }

  if (strncmp(linea, "V,", 2) == 0) {
    mostrarLectura(linea + 2);
  } else if (strncmp(linea, "OK,", 3) == 0) {
    Serial.print(F("El sensor confirmo: "));
    Serial.println(linea + 3);
  } else if (strncmp(linea, "ER,", 3) == 0) {
    Serial.print(F("El sensor rechazo o aviso: "));
    Serial.println(linea + 3);
  }
}

void leerBluetooth() {
  while (bt.available()) {
    char c = bt.read();
    if (c == '$') { largoTrama = 0; recibiendo = true; continue; }
    if (!recibiendo || c == '\r') continue;
    if (c == '\n') {
      trama[largoTrama] = '\0';
      recibiendo = false;
      procesarTrama(trama);
    } else if (largoTrama >= sizeof(trama) - 1) {
      recibiendo = false;           // trama demasiado larga: se descarta
    } else {
      trama[largoTrama++] = c;
    }
  }
}

// Traduce lo que escribe el operador a un comando del protocolo.
void procesarOrden(char *texto) {
  for (char *p = texto; *p; p++) *p = tolower(*p);
  if (strcmp(texto, "on") == 0)        enviarComando("M,1");
  else if (strcmp(texto, "off") == 0)  enviarComando("M,0");
  else if (strcmp(texto, "auto") == 0) enviarComando("A");
  else if (strncmp(texto, "u ", 2) == 0) {
    long valor = atol(texto + 2);
    char contenido[16];
    snprintf(contenido, sizeof(contenido), "U,%ld", valor);
    enviarComando(contenido);       // el sensor decide si el valor es valido
  } else {
    Serial.println(F("Ordenes: u 30 | on | off | auto"));
  }
}

void leerTeclado() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      orden[largoOrden] = '\0';
      largoOrden = 0;
      if (orden[0] != '\0') procesarOrden(orden);
    } else if (largoOrden < sizeof(orden) - 1) {
      orden[largoOrden++] = c;
    }
  }
}

void setup() {
  pinMode(PIN_LED_ENLACE, OUTPUT);
  pinMode(PIN_LED_ALARMA, OUTPUT);
  Serial.begin(9600);
  bt.begin(9600);
  Serial.println(F("Control de vibracion listo. Ordenes: u 30 | on | off | auto"));
}

void loop() {
  leerBluetooth();
  leerTeclado();
  unsigned long ahora = millis();

  if (ahora - ultimoLatido >= PERIODO_LATIDO) {
    ultimoLatido = ahora;
    enviarComando("P");             // le avisa al sensor que el control sigue conectado
  }

  if (!sinEnlace && ahora - ultimoDato > LIMITE_SIN_DATOS) {
    sinEnlace = true;
    digitalWrite(PIN_LED_ENLACE, HIGH);
    Serial.println(F("SIN ENLACE CON EL NODO SENSOR"));
  }
}
