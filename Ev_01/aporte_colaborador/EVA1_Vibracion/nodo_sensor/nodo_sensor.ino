// ============================================================================
// EVA1 - Aplicaciones Moviles para IoT (TI3V42) - INACAP Apoquindo
// Caso: Monitoreo de vibracion con activacion de alarma
//
// NODO SENSOR (microcontrolador 1) - Arduino UNO + SW-420 + buzzer/LED + HC-05
//
// Que hace:
//  - Cuenta los pulsos del sensor de vibracion SW-420 en ventanas de 1 s
//    (intensidad = eventos por segundo), sin bloquear el programa.
//  - Algoritmo que diferencia tres situaciones:
//      N = NORMAL       : intensidad bajo el umbral.
//      T = TRANSITORIA  : supera el umbral, pero aun no se confirma
//                         (un golpe aislado, una puerta que se cierra).
//      A = ANOMALA      : supera el umbral durante VENTANAS_CONFIRMAR ventanas
//                         seguidas -> vibracion sostenida -> se activa la alarma.
//      F = FALLA SENSOR : el sensor no responde -> alarma encendida (estado seguro).
//    La alarma se apaga solo despues de VENTANAS_LIBERAR ventanas tranquilas
//    (histeresis), para que no "parpadee" en el limite.
//  - Envia cada lectura por Bluetooth con formato $contenido*CS.
//  - Recibe comandos, los valida (checksum, formato, rango, largo) y confirma.
//  - Si se pierde el enlace en modo manual, vuelve solo al modo automatico.
// ============================================================================

#include <SoftwareSerial.h>

// ---------------------------- Pines -----------------------------------------
const byte PIN_SENSOR = 2;   // DO del SW-420 (pin 2 = interrupcion externa 0)
const byte PIN_ALARMA = 8;   // buzzer activo + LED rojo en paralelo
const byte PIN_LED_OK = 9;   // LED verde: parpadea si el sistema esta vivo
const byte BT_RX = 10;       // va al TX del HC-05
const byte BT_TX = 11;       // va al RX del HC-05, con divisor 1k/2k

SoftwareSerial bt(BT_RX, BT_TX);

// ---------------------------- Parametros ------------------------------------
const unsigned long PERIODO_VENTANA   = 1000;  // ms: una lectura por segundo
const unsigned long LIMITE_SIN_ENLACE = 5000;  // ms sin noticias del control
const unsigned long LIMITE_SENSOR_FIJO = 3000; // ms con la salida pegada en ALTO
const int  UMBRAL_MIN = 1;          // eventos/s
const int  UMBRAL_MAX = 500;        // eventos/s
const byte VENTANAS_CONFIRMAR = 3;  // ventanas seguidas sobre el umbral = anomalia
const byte VENTANAS_LIBERAR   = 3;  // ventanas seguidas bajo el umbral = normal

// ---------------------------- Estado ----------------------------------------
volatile unsigned int pulsos = 0;   // lo incrementa la interrupcion
int  umbral = 20;                   // eventos/s, configurable a distancia
bool modoManual = false;
bool alarmaManual = false;
bool alarmaAuto = false;            // decision del algoritmo
byte ventanasSobre = 0;
byte ventanasBajo = 0;
char estado = 'N';                  // N, T, A o F
unsigned long ultimaVentana = 0;
unsigned long ultimoContacto = 0;
unsigned long desdeAlto = 0;        // desde cuando la salida del sensor esta en ALTO
unsigned int  rechazados = 0;       // registro de mensajes invalidos (seguridad)

char comando[24];                   // buffer de recepcion con largo maximo fijo
byte largo = 0;
bool recibiendo = false;

// ---------------------------- Interrupcion ----------------------------------
// Cada flanco de subida del SW-420 es un "evento" de vibracion.
void contarPulso() {
  pulsos++;
}

// ---------------------------- Protocolo -------------------------------------
// Checksum XOR de todos los caracteres del contenido (como NMEA).
byte checksum(const char *texto) {
  byte cs = 0;
  for (const char *p = texto; *p; p++) cs ^= *p;
  return cs;
}

void enviar(const char *contenido) {
  char trama[40];
  snprintf(trama, sizeof(trama), "$%s*%02X", contenido, checksum(contenido));
  bt.println(trama);
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
  char respuesta[20];
  snprintf(respuesta, sizeof(respuesta), "ER,%s", motivo);
  enviar(respuesta);
}

bool esHexa(const char *texto) {
  return strlen(texto) == 2 && isxdigit(texto[0]) && isxdigit(texto[1]);
}

// Lista blanca de comandos (minimo privilegio, ISA/IEC 62443):
//   P      latido del control
//   A      volver a modo automatico
//   M,1    alarma manual encendida
//   M,0    alarma manual apagada
//   U,n    nuevo umbral en eventos/s (UMBRAL_MIN..UMBRAL_MAX)
// Todo lo demas se rechaza.
void procesarComando(char *linea) {
  // 1) Formato: debe traer "*" y checksum
  char *asterisco = strchr(linea, '*');
  if (asterisco == NULL) { rechazar("FORMATO"); return; }
  *asterisco = '\0';
  const char *recibido = asterisco + 1;

  // 2) Integridad: el checksum debe coincidir
  if (!esHexa(recibido) || (byte)strtol(recibido, NULL, 16) != checksum(linea)) {
    rechazar("CHECKSUM");
    return;
  }

  ultimoContacto = millis();        // solo un mensaje valido cuenta como enlace vivo

  if (strcmp(linea, "P") == 0) return;            // latido: no se responde

  if (strcmp(linea, "A") == 0) {
    modoManual = false;
    enviar("OK,A");
  } else if (strcmp(linea, "M,1") == 0) {
    modoManual = true;
    alarmaManual = true;
    enviar("OK,M1");
  } else if (strcmp(linea, "M,0") == 0) {
    modoManual = true;
    alarmaManual = false;
    enviar("OK,M0");
  } else if (strncmp(linea, "U,", 2) == 0) {
    // 3) Rango: solo numeros enteros dentro de los limites
    char *fin;
    long valor = strtol(linea + 2, &fin, 10);
    if (fin == linea + 2 || *fin != '\0' || valor < UMBRAL_MIN || valor > UMBRAL_MAX) {
      rechazar("RANGO");
      return;
    }
    umbral = (int)valor;
    ventanasSobre = 0;              // el algoritmo parte de cero con el nuevo umbral
    ventanasBajo = 0;
    char respuesta[12];
    snprintf(respuesta, sizeof(respuesta), "OK,U%d", umbral);
    enviar(respuesta);
  } else {
    rechazar("COMANDO");
  }
}

// Lee el Bluetooth caracter a caracter, sin esperar.
void leerBluetooth() {
  while (bt.available()) {
    char c = bt.read();
    if (c == '#') { largo = 0; recibiendo = true; continue; }  // inicio de comando
    if (!recibiendo || c == '\r') continue;
    if (c == '\n') {
      comando[largo] = '\0';
      recibiendo = false;
      procesarComando(comando);
    } else if (largo >= sizeof(comando) - 1) {
      // 4) Largo maximo: se descarta entero para no desbordar el buffer
      recibiendo = false;
      rechazar("LARGO");
    } else {
      comando[largo++] = c;
    }
  }
}

// ---------------------------- Algoritmo -------------------------------------
// Vigila si la salida del sensor quedo pegada en ALTO (cable cortado con
// INPUT_PULLUP, o modulo danado). Se llama en cada vuelta del loop.
void vigilarSensor(unsigned long ahora) {
  if (digitalRead(PIN_SENSOR) == HIGH) {
    if (desdeAlto == 0) desdeAlto = ahora;
  } else {
    desdeAlto = 0;
  }
}

bool sensorConFalla(unsigned long ahora, unsigned int eventos) {
  return eventos == 0 && desdeAlto != 0 && ahora - desdeAlto >= LIMITE_SENSOR_FIJO;
}

// Clasifica la ventana y decide la alarma automatica.
void clasificar(unsigned int eventos, bool falla) {
  if (falla) {
    estado = 'F';
    alarmaAuto = true;              // estado seguro: ante la duda, alarma encendida
    ventanasSobre = 0;
    ventanasBajo = 0;
    return;
  }

  if (eventos >= (unsigned int)umbral) {
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
    if (alarmaAuto && ventanasBajo < VENTANAS_LIBERAR) {
      estado = 'A';                 // histeresis: espera a confirmar que se calmo
    } else {
      estado = 'N';
      alarmaAuto = false;
    }
  }
}

// ---------------------------- Programa --------------------------------------
void setup() {
  pinMode(PIN_SENSOR, INPUT_PULLUP);
  pinMode(PIN_ALARMA, OUTPUT);
  pinMode(PIN_LED_OK, OUTPUT);
  digitalWrite(PIN_ALARMA, LOW);

  Serial.begin(9600);
  bt.begin(9600);
  attachInterrupt(digitalPinToInterrupt(PIN_SENSOR), contarPulso, RISING);

  ultimaVentana = millis();
  Serial.println(F("Nodo sensor de vibracion listo"));
}

void loop() {
  leerBluetooth();
  unsigned long ahora = millis();
  vigilarSensor(ahora);

  // Sin enlace, el manual pierde sentido: vuelve al automatico, que protege solo.
  if (modoManual && ahora - ultimoContacto > LIMITE_SIN_ENLACE) {
    modoManual = false;
    enviar("ER,SIN_ENLACE");
  }

  // Lectura periodica con millis(), sin delay().
  if (ahora - ultimaVentana >= PERIODO_VENTANA) {
    ultimaVentana = ahora;

    noInterrupts();                 // copia y reinicia el contador de forma atomica
    unsigned int eventos = pulsos;
    pulsos = 0;
    interrupts();

    clasificar(eventos, sensorConFalla(ahora, eventos));

    bool alarma = modoManual ? alarmaManual : alarmaAuto;
    digitalWrite(PIN_ALARMA, alarma ? HIGH : LOW);
    digitalWrite(PIN_LED_OK, !digitalRead(PIN_LED_OK));  // latido local

    // Trama de lectura: V,<eventos/s>,<estado>,<alarma>,<modo>,<umbral>
    char lectura[32];
    snprintf(lectura, sizeof(lectura), "V,%u,%c,%d,%c,%d",
             eventos, estado, alarma ? 1 : 0, modoManual ? 'M' : 'A', umbral);
    enviar(lectura);
  }
}
