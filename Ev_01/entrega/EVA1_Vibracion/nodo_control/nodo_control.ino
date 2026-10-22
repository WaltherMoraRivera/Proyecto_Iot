// ============================================================================
// EVA1 - Aplicaciones Moviles para IoT (TI3V42) - INACAP Apoquindo
// Caso: Monitoreo de vibracion con activacion de alarma
// Integrantes: Walther Mora, Maximiliano Soto
//
// NODO DE CONTROL (microcontrolador 2) - ESP32 + WiFi (MQTT) + LEDs
//
// Que hace:
//  - Recibe las tramas del nodo sensor, valida checksum y firma, las interpreta
//    y las muestra en el Monitor Serie en lenguaje claro.
//  - Traduce lo que escribe el operador a comandos del protocolo:
//        u 400  -> cambia el umbral a 400 mg
//        on     -> alarma manual encendida
//        off    -> alarma manual apagada
//        auto   -> vuelve al modo automatico
//  - Envia un latido cada 2 s para que el sensor sepa que el enlace sigue vivo.
//  - LED enlace (GPIO 26): se enciende si deja de recibir lecturas.
//  - LED alarma (GPIO 25): se enciende cuando el sensor reporta la alarma activa.
//
// Librerias: PubSubClient (Nick O'Leary) desde el Gestor de librerias.
// Placa: "ESP32 Dev Module" (paquete esp32 de Espressif).
// ============================================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include "mbedtls/md.h"

// ---------------------------- Red (igual que en el nodo sensor) -------------
const char *WIFI_SSID  = "Wokwi-GUEST";
const char *WIFI_CLAVE = "";
const int   WIFI_CANAL = 6;      // 6 acelera la conexion en Wokwi; 0 = buscar canal

const char *MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PUERTO = 1883;
const char *TOPICO_DATOS = "inacap/ti3v42/vib-wm-ms/datos";   // sensor -> control
const char *TOPICO_CMD   = "inacap/ti3v42/vib-wm-ms/cmd";     // control -> sensor

// Misma clave compartida que el nodo sensor (clave de demostracion).
const char *CLAVE_HMAC = "vib-demo-2026-cambiar";

// ---------------------------- Pines y tiempos -------------------------------
const byte PIN_LED_ALARMA = 25;   // repetidor de la alarma remota
const byte PIN_LED_ENLACE = 26;   // enlace caido

const unsigned long PERIODO_LATIDO    = 2000;  // ms entre latidos
const unsigned long LIMITE_SIN_DATOS  = 3000;  // ms sin lecturas del sensor
const unsigned long PERIODO_REINTENTO = 5000;  // ms entre intentos de reconexion MQTT

unsigned long ultimoLatido = 0;
unsigned long ultimoDato = 0;
unsigned long ultimoIntentoMqtt = 0;
unsigned long seq = 0;            // numero de secuencia del ultimo comando enviado
bool sincronizado = false;        // ya se recibio la secuencia del sensor
bool sinEnlace = false;

char orden[24];
byte largoOrden = 0;

WiFiClient red;
PubSubClient mqtt(red);

// ---------------------------- Protocolo (igual que en el nodo sensor) -------
byte checksum(const char *texto) {
  byte cs = 0;
  for (const char *p = texto; *p; p++) cs ^= *p;
  return cs;
}

void firmar(const char *texto, char salida[9]) {
  byte mac[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                  (const unsigned char *)CLAVE_HMAC, strlen(CLAVE_HMAC),
                  (const unsigned char *)texto, strlen(texto), mac);
  for (int i = 0; i < 4; i++) sprintf(salida + 2 * i, "%02x", mac[i]);
}

bool igualesSeguro(const char *a, const char *b, size_t n) {
  byte dif = 0;
  for (size_t i = 0; i < n; i++) dif |= a[i] ^ b[i];
  return dif == 0;
}

bool esHexa(const char *texto, size_t n) {
  if (strlen(texto) != n) return false;
  for (size_t i = 0; i < n; i++) if (!isxdigit(texto[i])) return false;
  return true;
}

// Arma "#<seq>,<orden>*CS;FIRMA" y lo publica. Cada comando lleva una
// secuencia nueva, asi el sensor rechaza los comandos repetidos.
void enviarComando(const char *contenidoOrden) {
  if (!mqtt.connected()) {
    Serial.println(F("Sin conexion MQTT: comando no enviado"));
    return;
  }
  seq++;
  char contenido[40], cuerpo[48], firma[9], trama[64];
  snprintf(contenido, sizeof(contenido), "%lu,%s", seq, contenidoOrden);
  snprintf(cuerpo, sizeof(cuerpo), "#%s", contenido);
  firmar(cuerpo, firma);
  snprintf(trama, sizeof(trama), "%s*%02X;%s", cuerpo, checksum(contenido), firma);
  mqtt.publish(TOPICO_CMD, trama);
}

// Traduce la letra de estado a texto.
const char *textoEstado(char e) {
  switch (e) {
    case 'N': return "NORMAL";
    case 'T': return "TRANSITORIA (golpe aislado)";
    case 'A': return "ANOMALA";
    case 'F': return "FALLA DEL SENSOR";
    default:  return "?";
  }
}

// Interpreta "mg,estado,alarma,modo,umbral,seq" y lo muestra.
void mostrarLectura(const char *datos) {
  int intensidad, alarma, umbral;
  char estado, modo;
  unsigned long seqSensor;
  if (sscanf(datos, "%d,%c,%d,%c,%d,%lu", &intensidad, &estado, &alarma, &modo,
             &umbral, &seqSensor) != 6) {
    Serial.println(F("Lectura con formato raro, descartada"));
    return;
  }
  // Si el control se reinicio, retoma la secuencia desde la que acepto el sensor.
  if (seqSensor > seq) seq = seqSensor;
  sincronizado = true;
  digitalWrite(PIN_LED_ALARMA, alarma ? HIGH : LOW);

  Serial.print(F("Vibracion: "));
  if (intensidad < 0) Serial.print(F("--"));
  else Serial.print(intensidad);
  Serial.print(F(" mg (umbral "));
  Serial.print(umbral);
  Serial.print(F(") | Estado: "));
  Serial.print(textoEstado(estado));
  Serial.print(alarma ? F(" | Alarma: ENCENDIDA") : F(" | Alarma: apagada"));
  Serial.println(modo == 'M' ? F(" | Modo: MANUAL") : F(" | Modo: AUTO"));
}

void procesarTrama(char *trama) {
  char *pyc = strrchr(trama, ';');
  char *asterisco = strrchr(trama, '*');
  if (trama[0] != '$' || pyc == NULL || asterisco == NULL || asterisco > pyc) {
    Serial.println(F("[SEGURIDAD] Trama con formato invalido, descartada"));
    return;
  }
  *pyc = '\0';
  *asterisco = '\0';
  const char *firmaRecibida = pyc + 1;
  const char *csRecibido = asterisco + 1;
  char *contenido = trama + 1;

  if (!esHexa(csRecibido, 2) || (byte)strtol(csRecibido, NULL, 16) != checksum(contenido)) {
    Serial.println(F("[SEGURIDAD] Checksum incorrecto, trama descartada"));
    return;
  }
  char firmaEsperada[9];
  firmar(trama, firmaEsperada);
  if (!esHexa(firmaRecibida, 8) || !igualesSeguro(firmaRecibida, firmaEsperada, 8)) {
    Serial.println(F("[SEGURIDAD] Firma invalida, trama descartada"));
    return;
  }

  if (strcmp(contenido, "ER,OFFLINE") == 0) {   // ultima voluntad del sensor
    Serial.println(F("El broker avisa: el NODO SENSOR SE DESCONECTO"));
    return;
  }

  ultimoDato = millis();
  if (sinEnlace) {
    sinEnlace = false;
    digitalWrite(PIN_LED_ENLACE, LOW);
    Serial.println(F("Enlace recuperado"));
  }

  if (strncmp(contenido, "V,", 2) == 0) {
    mostrarLectura(contenido + 2);
  } else if (strncmp(contenido, "OK,", 3) == 0) {
    Serial.print(F("El sensor confirmo: "));
    Serial.println(contenido + 3);
  } else if (strncmp(contenido, "ER,", 3) == 0) {
    Serial.print(F("El sensor rechazo o aviso: "));
    Serial.println(contenido + 3);
  }
}

void alRecibir(char *topico, byte *datos, unsigned int largo) {
  char trama[80];
  if (largo >= sizeof(trama)) {
    Serial.println(F("[SEGURIDAD] Trama demasiado larga, descartada"));
    return;
  }
  memcpy(trama, datos, largo);
  trama[largo] = '\0';
  procesarTrama(trama);
}

void conectarMqtt() {
  char id[24];
  snprintf(id, sizeof(id), "vib-control-%04X", (unsigned)(esp_random() & 0xFFFF));
  Serial.print(F("Conectando a MQTT... "));
  if (mqtt.connect(id)) {
    mqtt.subscribe(TOPICO_DATOS);
    Serial.println(F("listo"));
  } else {
    Serial.print(F("fallo, estado "));
    Serial.println(mqtt.state());
  }
}

void mantenerRed(unsigned long ahora) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!mqtt.connected() && ahora - ultimoIntentoMqtt >= PERIODO_REINTENTO) {
    ultimoIntentoMqtt = ahora;
    conectarMqtt();
  }
  mqtt.loop();
}

// ---------------------------- Operador --------------------------------------
// Traduce lo que escribe el operador a un comando del protocolo.
void procesarOrden(char *texto) {
  for (char *p = texto; *p; p++) *p = tolower(*p);
  if (!sincronizado) {
    Serial.println(F("Aun no llegan lecturas del sensor; espere el enlace"));
    return;
  }
  if (strcmp(texto, "on") == 0)        enviarComando("M,1");
  else if (strcmp(texto, "off") == 0)  enviarComando("M,0");
  else if (strcmp(texto, "auto") == 0) enviarComando("A");
  else if (strncmp(texto, "u ", 2) == 0) {
    long valor = atol(texto + 2);
    char contenido[16];
    snprintf(contenido, sizeof(contenido), "U,%ld", valor);
    enviarComando(contenido);       // el sensor decide si el valor es valido
  } else {
    Serial.println(F("Ordenes: u 400 | on | off | auto"));
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
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  if (WIFI_CANAL > 0) WiFi.begin(WIFI_SSID, WIFI_CLAVE, WIFI_CANAL);
  else WiFi.begin(WIFI_SSID, WIFI_CLAVE);
  mqtt.setServer(MQTT_HOST, MQTT_PUERTO);
  mqtt.setCallback(alRecibir);

  Serial.println(F("Control de vibracion listo. Ordenes: u 400 | on | off | auto"));
}

void loop() {
  mantenerRed(millis());
  leerTeclado();
  // Se toma la hora despues de atender la red: si llego una lectura, ultimoDato
  // ya quedo actualizado y la resta "ahora - ultimoDato" no se da vuelta.
  unsigned long ahora = millis();

  if (sincronizado && ahora - ultimoLatido >= PERIODO_LATIDO) {
    ultimoLatido = ahora;
    enviarComando("P");             // le avisa al sensor que el control sigue conectado
  }

  if (!sinEnlace && ahora - ultimoDato > LIMITE_SIN_DATOS) {
    sinEnlace = true;
    digitalWrite(PIN_LED_ENLACE, HIGH);
    Serial.println(F("SIN ENLACE CON EL NODO SENSOR"));
  }
}
