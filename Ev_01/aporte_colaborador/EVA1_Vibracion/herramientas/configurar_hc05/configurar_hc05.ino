// ============================================================================
// EVA1 - Herramienta: configurar los HC-05 (cambiar el emparejamiento por defecto)
//
// Medida de seguridad: el HC-05 viene con PIN 1234 y acepta a cualquiera.
// Este sketch hace de "puente" entre el Monitor Serie y el HC-05 en modo AT
// para cambiar el PIN y fijar el maestro a la direccion del esclavo.
//
// Uso:
//  1. Conectar el HC-05 igual que en los nodos (TX->10, RX->11 con divisor).
//  2. Mantener presionado el boton del HC-05 (o pin EN/KEY a 5V) al energizarlo:
//     el LED parpadea lento (cada 2 s) = modo AT.
//  3. Monitor Serie a 9600 con "Ambos NL & CR". Escribir los comandos:
//
//   ESCLAVO (nodo sensor):           MAESTRO (nodo de control):
//     AT                -> OK          AT
//     AT+NAME=VIB_SENSOR               AT+NAME=VIB_CONTROL
//     AT+PSWD="4826"   (PIN propio)    AT+PSWD="4826"   (el mismo PIN)
//     AT+ROLE=0                         AT+ROLE=1
//     AT+UART=9600,0,0                  AT+UART=9600,0,0
//     AT+ADDR?  -> anotar direccion     AT+CMODE=0  (solo a una direccion)
//                                        AT+BIND=98d3,31,f5xxxx (la del esclavo,
//                                                 con comas en vez de ":")
//  4. Desenergizar y volver a cargar el sketch del nodo correspondiente.
//
// Nota: en algunos firmware el PIN va sin comillas: AT+PSWD=4826
// No usar 1234, 0000 ni el mismo PIN que muestra este ejemplo.
// ============================================================================

#include <SoftwareSerial.h>

SoftwareSerial bt(10, 11);   // RX, TX

void setup() {
  Serial.begin(9600);
  bt.begin(38400);           // velocidad fija del modo AT del HC-05
  Serial.println(F("Puente AT listo. Escriba AT y presione Enter."));
}

void loop() {
  if (bt.available()) Serial.write(bt.read());
  if (Serial.available()) bt.write(Serial.read());
}
