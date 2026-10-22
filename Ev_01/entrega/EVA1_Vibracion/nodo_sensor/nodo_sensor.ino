// ============================================================================
// EVA1 - Aplicaciones Moviles para IoT (TI3V42) - INACAP Apoquindo
// Caso: Monitoreo de vibracion con activacion de alarma
// Integrantes: Walther Mora, Maximiliano Soto
//
// NODO SENSOR (microcontrolador 1) - ESP32 + MPU6050 + buzzer/LED + WiFi (MQTT)
//
// Que hace:
//  - Lee el acelerometro MPU6050 cada 20 ms (50 muestras/s) sin bloquear y, en
//    ventanas de 1 s, calcula la intensidad de vibracion: el pico de desviacion
//    de la aceleracion respecto de 1 g (gravedad), en mili-g (mg).
//  - Algoritmo que diferencia cuatro situaciones:
//      N = NORMAL       : intensidad bajo el umbral.
//      T = TRANSITORIA  : supera el umbral, pero aun no se confirma
//                         (un golpe aislado, una puerta que se cierra).
//      A = ANOMALA      : supera el umbral VENTANAS_CONFIRMAR ventanas seguidas
//                         -> vibracion sostenida -> se activa la alarma.
//      F = FALLA SENSOR : el MPU6050 no responde -> alarma encendida (estado seguro).
//    La alarma se apaga solo despues de VENTANAS_LIBERAR ventanas tranquilas
//    (histeresis), para que no "parpadee" en el limite.
//  - Envia cada lectura por WiFi (MQTT) al nodo de control, firmada.
//  - Recibe comandos, los valida (checksum, firma, secuencia, lista blanca,
//    rango y largo) y confirma cada uno que acepta.
//  - Si se pierde el enlace en modo manual, vuelve solo al modo automatico.
//  - La alarma local funciona aunque no haya WiFi: la red no manda la seguridad.
//
// Librerias: PubSubClient (Nick O'Leary) desde el Gestor de librerias.
// Placa: "ESP32 Dev Module" (paquete esp32 de Espressif).
// ============================================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "mbedtls/md.h"

// ---------------------------- Red -------------------------------------------
// En Wokwi la red "Wokwi-GUEST" no tiene clave. Con hardware real se cambia
// por la red del lugar (y conviene no subir esa clave al repositorio).
const char *WIFI_SSID  = "Wokwi-GUEST";
const char *WIFI_CLAVE = "";
const int   WIFI_CANAL = 6;      // 6 acelera la conexion en Wokwi; 0 = buscar canal

const char *MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PUERTO = 1883;
// Topicos propios del proyecto: "conducto" unico entre los dos nodos.
const char *TOPICO_DATOS = "inacap/ti3v42/vib-wm-ms/datos";   // sensor -> control
const char *TOPICO_CMD   = "inacap/ti3v42/vib-wm-ms/cmd";     // control -> sensor

// Clave compartida para firmar los mensajes (HMAC-SHA256). Es la misma en los
// dos nodos. Clave de demostracion: en un equipo real se cambia y no se publica.
const char *CLAVE_HMAC = "vib-demo-2026-cambiar";

// ---------------------------- Pines -----------------------------------------
const byte PIN_SDA = 21;          // MPU6050 SDA
const byte PIN_SCL = 22;          // MPU6050 SCL
const byte PIN_BUZZER = 18;       // buzzer activo
const byte PIN_LED_ALARMA = 19;   // LED rojo, se enciende junto al buzzer
const byte PIN_LED_VIDA = 23;     // LED verde: parpadea si el nodo esta vivo

// ---------------------------- Parametros ------------------------------------
const unsigned long PERIODO_MUESTRA   = 20;    // ms entre muestras (50 Hz)
const unsigned long PERIODO_VENTANA   = 1000;  // ms: una lectura por segundo
const unsigned long LIMITE_SIN_ENLACE = 5000;  // ms sin mensajes validos del control
const unsigned long PERIODO_REINTENTO = 5000;  // ms entre intentos de reconexion MQTT
const int  UMBRAL_MIN = 50;        // mg
const int  UMBRAL_MAX = 4000;      // mg (el MPU6050 en +-2 g llega a ~2000 mg de desviacion)
const byte VENTANAS_CONFIRMAR = 3; // ventanas seguidas sobre el umbral = anomalia
const byte VENTANAS_LIBERAR   = 3; // ventanas seguidas bajo el umbral = normal
const byte MPU_DIR = 0x68;         // direccion I2C del MPU6050

// ---------------------------- Estado ----------------------------------------
int  umbral = 300;                 // mg, configurable a distancia
bool modoManual = false;
bool alarmaManual = false;
bool alarmaAuto = false;           // decision del algoritmo
byte ventanasSobre = 0;
byte ventanasBajo = 0;
char estado = 'N';                 // N, T, A o F
int  picoVentana = 0;              // mg, pico de la ventana en curso
unsigned int muestrasOk = 0;       // lecturas validas en la ventana en curso
unsigned long ultimaMuestra = 0;
unsigned long ultimaVentana = 0;
unsigned long ultimoContacto = 0;
unsigned long ultimoIntentoMqtt = 0;
unsigned long ultimoSeq = 0;       // ultimo numero de secuencia aceptado
unsigned int  rechazados = 0;      // registro de mensajes invalidos (seguridad)

WiFiClient red;
PubSubClient mqtt(red);

// ---------------------------- Protocolo -------------------------------------
// Trama:  <inicio><contenido>*<CS>;<FIRMA>
//   inicio : '$' sensor -> control, '#' control -> sensor
//   CS     : XOR de los caracteres del contenido, 2 digitos hex (como NMEA)
//   FIRMA  : primeros 4 bytes del HMAC-SHA256 de "<inicio><contenido>", 8 hex
// Ejemplo: $V,412,A,1,A,300,57*61;eef8c861

byte checksum(const char *texto) {
  byte cs = 0;
  for (const char *p = texto; *p; p++) cs ^= *p;
  return cs;
}

// Firma: solo quien conoce CLAVE_HMAC puede calcularla.
void firmar(const char *texto, char salida[9]) {
  byte mac[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                  (const unsigned char *)CLAVE_HMAC, strlen(CLAVE_HMAC),
                  (const unsigned char *)texto, strlen(texto), mac);
  for (int i = 0; i < 4; i++) sprintf(salida + 2 * i, "%02x", mac[i]);
}

// Compara sin cortar al primer byte distinto, para no filtrar informacion por el tiempo.
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

void enviar(const char *contenido) {
  char cuerpo[64], firma[9], trama[80];
  snprintf(cuerpo, sizeof(cuerpo), "$%s", contenido);
  firmar(cuerpo, firma);
  snprintf(trama, sizeof(trama), "%s*%02X;%s", cuerpo, checksum(contenido), firma);
  if (mqtt.connected()) mqtt.publish(TOPICO_DATOS, trama);
  Serial.println(trama);            // copia en el Monitor Serie para depurar
}

// Rechaza un mensaje: lo registra (contador + Monitor Serie) y avisa al control.
// Nunca ejecuta nada del mensaje rechazado.
void rechazar(const char *motivo) {
  rechazados++;
  Serial.print(F("[SEGURIDAD] Mensaje rechazado #"));
  Serial.print(rechazados);
  Serial.print(F(": "));
  Serial.println(motivo);
  char respuesta[24];
  snprintf(respuesta, sizeof(respuesta), "ER,%s", motivo);
  enviar(respuesta);
}

// Lista blanca de comandos (minimo privilegio, ISA/IEC 62443):
//   P      latido del control
//   A      volver a modo automatico
//   M,1    alarma manual encendida
//   M,0    alarma manual apagada
//   U,n    nuevo umbral en mg (UMBRAL_MIN..UMBRAL_MAX)
// Todo lo demas se rechaza. Llega por ejemplo "#58,U,400*6C;2ec68e41".
void procesarComando(char *trama) {
  // 1) Formato: '#', contenido, '*', checksum, ';', firma
  char *pyc = strrchr(trama, ';');
  char *asterisco = strrchr(trama, '*');
  if (trama[0] != '#' || pyc == NULL || asterisco == NULL || asterisco > pyc) {
    rechazar("FORMATO");
    return;
  }
  *pyc = '\0';
  *asterisco = '\0';
  const char *firmaRecibida = pyc + 1;
  const char *csRecibido = asterisco + 1;
  char *contenido = trama + 1;

  // 2) Integridad: el checksum debe coincidir (detecta bytes alterados)
  if (!esHexa(csRecibido, 2) || (byte)strtol(csRecibido, NULL, 16) != checksum(contenido)) {
    rechazar("CHECKSUM");
    return;
  }

  // 3) Autenticidad: la firma debe calzar con la clave compartida
  char firmaEsperada[9];
  firmar(trama, firmaEsperada);     // trama = "#<contenido>" (ya cortada)
  if (!esHexa(firmaRecibida, 8) || !igualesSeguro(firmaRecibida, firmaEsperada, 8)) {
    rechazar("FIRMA");
    return;
  }

  // 4) Secuencia: cada comando trae un numero mayor que el anterior (anti-repeticion)
  char *fin;
  unsigned long seq = strtoul(contenido, &fin, 10);
  if (fin == contenido || *fin != ',') { rechazar("FORMATO"); return; }
  if (seq <= ultimoSeq) { rechazar("REPETIDO"); return; }
  ultimoSeq = seq;
  ultimoContacto = millis();        // solo un mensaje autentico cuenta como enlace vivo
  const char *orden = fin + 1;

  if (strcmp(orden, "P") == 0) return;            // latido: no se responde

  if (strcmp(orden, "A") == 0) {
    modoManual = false;
    enviar("OK,A");
  } else if (strcmp(orden, "M,1") == 0) {
    modoManual = true;
    alarmaManual = true;
    enviar("OK,M1");
  } else if (strcmp(orden, "M,0") == 0) {
    modoManual = true;
    alarmaManual = false;
    enviar("OK,M0");
  } else if (strncmp(orden, "U,", 2) == 0) {
    // 5) Rango: solo numeros enteros dentro de los limites
    char *finNum;
    long valor = strtol(orden + 2, &finNum, 10);
    if (finNum == orden + 2 || *finNum != '\0' || valor < UMBRAL_MIN || valor > UMBRAL_MAX) {
      rechazar("RANGO");
      return;
    }
    umbral = (int)valor;
    ventanasSobre = 0;              // el algoritmo parte de cero con el nuevo umbral
    ventanasBajo = 0;
    char respuesta[16];
    snprintf(respuesta, sizeof(respuesta), "OK,U%d", umbral);
    enviar(respuesta);
  } else {
    rechazar("COMANDO");
  }
}

// Llega un mensaje MQTT al topico de comandos.
void alRecibir(char *topico, byte *datos, unsigned int largo) {
  char trama[64];                   // buffer de recepcion con largo maximo fijo
  // 6) Largo maximo: lo que no cabe se descarta entero
  if (largo >= sizeof(trama)) { rechazar("LARGO"); return; }
  memcpy(trama, datos, largo);
  trama[largo] = '\0';
  procesarComando(trama);
}

// ---------------------------- Red -------------------------------------------
void conectarMqtt() {
  char id[24];
  snprintf(id, sizeof(id), "vib-sensor-%04X", (unsigned)(esp_random() & 0xFFFF));
  // Ultima voluntad: si el sensor se cae, el broker avisa al control.
  char cuerpo[] = "$ER,OFFLINE";
  char firma[9], ultimaVoluntad[40];
  firmar(cuerpo, firma);
  snprintf(ultimaVoluntad, sizeof(ultimaVoluntad), "%s*%02X;%s", cuerpo, checksum("ER,OFFLINE"), firma);

  Serial.print(F("Conectando a MQTT... "));
  if (mqtt.connect(id, TOPICO_DATOS, 0, false, ultimaVoluntad)) {
    mqtt.subscribe(TOPICO_CMD);
    Serial.println(F("listo"));
  } else {
    Serial.print(F("fallo, estado "));
    Serial.println(mqtt.state());
  }
}

// Mantiene WiFi y MQTT sin detener el programa: un intento cada 5 s.
void mantenerRed(unsigned long ahora) {
  if (WiFi.status() != WL_CONNECTED) return;      // el ESP32 reintenta solo el WiFi
  if (!mqtt.connected() && ahora - ultimoIntentoMqtt >= PERIODO_REINTENTO) {
    ultimoIntentoMqtt = ahora;
    conectarMqtt();
  }
  mqtt.loop();
}

// ---------------------------- Sensor ----------------------------------------
bool mpuIniciar() {
  Wire.beginTransmission(MPU_DIR);
  Wire.write(0x6B);                 // PWR_MGMT_1
  Wire.write(0);                    // despertar el sensor
  return Wire.endTransmission() == 0;
}

// Lee la aceleracion y devuelve la desviacion respecto de 1 g, en mg.
// Devuelve -1 si el sensor no responde.
int leerDesviacionMg() {
  Wire.beginTransmission(MPU_DIR);
  Wire.write(0x3B);                 // ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(MPU_DIR, (uint8_t)6) != 6) return -1;
  int16_t ax = (Wire.read() << 8) | Wire.read();
  int16_t ay = (Wire.read() << 8) | Wire.read();
  int16_t az = (Wire.read() << 8) | Wire.read();
  // Rango +-2 g: 16384 cuentas por g.
  float gx = ax / 16384.0, gy = ay / 16384.0, gz = az / 16384.0;
  float magnitud = sqrt(gx * gx + gy * gy + gz * gz);
  return (int)(fabs(magnitud - 1.0) * 1000.0);
}

// ---------------------------- Algoritmo -------------------------------------
// Clasifica la ventana y decide la alarma automatica.
void clasificar(int intensidad, bool falla) {
  if (falla) {
    estado = 'F';
    alarmaAuto = true;              // estado seguro: ante la duda, alarma encendida
    ventanasSobre = 0;
    ventanasBajo = 0;
    return;
  }

  if (intensidad >= umbral) {
    ventanasBajo = 0;
    if (ventanasSobre < 255) ventanasSobre++;
    if (ventanasSobre >= VENTANAS_CONFIRMAR) {
      estado = 'A';                 // vibracion sostenida: anomalia confirmada
      alarmaAuto = true;
    } else if (!alarmaAuto) {
      estado = 'T';                 // golpe aislado: se observa, no se alarma aun
    }
  } else {
    ventanasSobre = 0;
    if (ventanasBajo < 255) ventanasBajo++;
    if (alarmaAuto && estado != 'F' && ventanasBajo < VENTANAS_LIBERAR) {
      estado = 'A';                 // histeresis: espera a confirmar que se calmo
    } else {
      estado = 'N';
      alarmaAuto = false;
    }
  }
}

// ---------------------------- Programa --------------------------------------
void setup() {
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_ALARMA, OUTPUT);
  pinMode(PIN_LED_VIDA, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_ALARMA, LOW);

  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);
  if (!mpuIniciar()) Serial.println(F("MPU6050 no responde"));

  WiFi.mode(WIFI_STA);
  if (WIFI_CANAL > 0) WiFi.begin(WIFI_SSID, WIFI_CLAVE, WIFI_CANAL);
  else WiFi.begin(WIFI_SSID, WIFI_CLAVE);
  mqtt.setServer(MQTT_HOST, MQTT_PUERTO);
  mqtt.setCallback(alRecibir);

  ultimaVentana = millis();
  Serial.println(F("Nodo sensor de vibracion listo"));
}

void loop() {
  mantenerRed(millis());
  // Se toma la hora despues de atender la red: si llego un comando, ultimoContacto
  // ya quedo actualizado y la resta "ahora - ultimoContacto" no se da vuelta.
  unsigned long ahora = millis();

  // Sin enlace, el manual pierde sentido: vuelve al automatico, que protege solo.
  if (modoManual && ahora - ultimoContacto > LIMITE_SIN_ENLACE) {
    modoManual = false;
    Serial.println(F("Sin enlace con el control: vuelve a modo AUTO"));
    enviar("ER,SIN_ENLACE");
  }

  // Muestreo periodico con millis(), sin delay().
  if (ahora - ultimaMuestra >= PERIODO_MUESTRA) {
    ultimaMuestra = ahora;
    int desviacion = leerDesviacionMg();
    if (desviacion >= 0) {
      muestrasOk++;
      if (desviacion > picoVentana) picoVentana = desviacion;
    }
  }

  // Cierre de ventana: clasificar, actuar y enviar la lectura.
  if (ahora - ultimaVentana >= PERIODO_VENTANA) {
    ultimaVentana = ahora;
    bool falla = (muestrasOk == 0);           // ni una lectura valida en 1 s
    int intensidad = falla ? -1 : picoVentana;
    if (falla) mpuIniciar();                  // intenta recuperar el sensor
    picoVentana = 0;
    muestrasOk = 0;

    clasificar(intensidad, falla);

    bool alarma = modoManual ? alarmaManual : alarmaAuto;
    digitalWrite(PIN_BUZZER, alarma ? HIGH : LOW);
    digitalWrite(PIN_LED_ALARMA, alarma ? HIGH : LOW);
    digitalWrite(PIN_LED_VIDA, !digitalRead(PIN_LED_VIDA));  // latido local

    // Lectura: V,<mg>,<estado>,<alarma>,<modo>,<umbral>,<ultima secuencia aceptada>
    char lectura[48];
    snprintf(lectura, sizeof(lectura), "V,%d,%c,%d,%c,%d,%lu",
             intensidad, estado, alarma ? 1 : 0, modoManual ? 'M' : 'A', umbral, ultimoSeq);
    enviar(lectura);
  }
}
