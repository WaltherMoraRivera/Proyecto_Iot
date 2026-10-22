"""
EVA1 - Probador de seguridad del protocolo (solo libreria estandar de Python 3).

Se conecta al mismo broker MQTT que los nodos y hace de "atacante" y de
"control legitimo" para comprobar que el nodo sensor:
  - acepta un comando bien firmado y con secuencia nueva,
  - rechaza checksum alterado, firma falsa, comando repetido, rango invalido,
    comando fuera de la lista blanca y mensajes demasiado largos.

Uso (con la simulacion del nodo sensor corriendo en Wokwi):
    python probar_seguridad.py            # muestra lecturas y corre las pruebas
    python probar_seguridad.py --escuchar # solo muestra lo que publica el sensor
"""
import hashlib
import hmac
import socket
import struct
import sys
import time

HOST, PUERTO = "broker.hivemq.com", 1883
TOPICO_DATOS = "inacap/ti3v42/vib-wm-ms/datos"
TOPICO_CMD = "inacap/ti3v42/vib-wm-ms/cmd"
CLAVE_HMAC = b"vib-demo-2026-cambiar"


# ---------------------------- Protocolo -------------------------------------
def checksum(texto: str) -> int:
    cs = 0
    for c in texto:
        cs ^= ord(c)
    return cs


def firmar(texto: str) -> str:
    return hmac.new(CLAVE_HMAC, texto.encode(), hashlib.sha256).hexdigest()[:8]


def trama(inicio: str, contenido: str, clave_ok=True, cs_ok=True) -> str:
    cuerpo = inicio + contenido
    firma = firmar(cuerpo) if clave_ok else "deadbeef"
    cs = checksum(contenido) ^ (0 if cs_ok else 0x01)
    return f"{cuerpo}*{cs:02X};{firma}"


def verificar(t: str) -> str:
    """Devuelve 'OK' o el motivo por el que la trama no es valida."""
    try:
        cuerpo, resto = t.rsplit("*", 1)
        cs, firma = resto.split(";")
    except ValueError:
        return "FORMATO"
    if int(cs, 16) != checksum(cuerpo[1:]):
        return "CHECKSUM"
    if not hmac.compare_digest(firma, firmar(cuerpo)):
        return "FIRMA"
    return "OK"


# ---------------------------- MQTT minimo (3.1.1) ---------------------------
def _largo(n: int) -> bytes:
    out = bytearray()
    while True:
        b, n = n % 128, n // 128
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def _cadena(s: str) -> bytes:
    b = s.encode()
    return struct.pack("!H", len(b)) + b


class Mqtt:
    def __init__(self, id_cliente: str):
        self.s = socket.create_connection((HOST, PUERTO), timeout=10)
        cuerpo = _cadena("MQTT") + bytes([4, 2]) + struct.pack("!H", 60) + _cadena(id_cliente)
        self.s.sendall(b"\x10" + _largo(len(cuerpo)) + cuerpo)
        if self._leer_paquete()[0] != 0x20:
            raise RuntimeError("el broker no acepto la conexion")
        self.s.settimeout(0.2)

    def suscribir(self, topico: str):
        cuerpo = struct.pack("!H", 1) + _cadena(topico) + b"\x00"
        self.s.sendall(b"\x82" + _largo(len(cuerpo)) + cuerpo)

    def publicar(self, topico: str, mensaje: str):
        cuerpo = _cadena(topico) + mensaje.encode()
        self.s.sendall(b"\x30" + _largo(len(cuerpo)) + cuerpo)

    def _leer_exacto(self, n: int) -> bytes:
        datos = b""
        while len(datos) < n:
            parte = self.s.recv(n - len(datos))
            if not parte:
                raise ConnectionError("conexion cerrada")
            datos += parte
        return datos

    def _leer_paquete(self):
        tipo = self._leer_exacto(1)[0]
        mult, largo = 1, 0
        while True:
            b = self._leer_exacto(1)[0]
            largo += (b & 0x7F) * mult
            mult *= 128
            if not b & 0x80:
                break
        return tipo, self._leer_exacto(largo)

    def mensajes(self, segundos: float):
        """Entrega los mensajes PUBLISH que lleguen durante 'segundos'."""
        fin = time.time() + segundos
        while time.time() < fin:
            try:
                tipo, cuerpo = self._leer_paquete()
            except socket.timeout:
                continue
            if tipo & 0xF0 == 0x30:
                n = struct.unpack("!H", cuerpo[:2])[0]
                yield cuerpo[2 + n:].decode(errors="replace")


# ---------------------------- Pruebas ---------------------------------------
def main():
    cli = Mqtt(f"probador-{int(time.time()) % 100000}")
    cli.suscribir(TOPICO_DATOS)

    if "--escuchar" in sys.argv:
        for m in cli.mensajes(10**9):
            print(f"{m:45s} -> {verificar(m)}")

    # 1) Esperar una lectura para conocer la ultima secuencia aceptada
    print("Esperando lecturas del nodo sensor...")
    seq = None
    for m in cli.mensajes(30):
        print("  recibido:", m, "->", verificar(m))
        if verificar(m) == "OK" and m.startswith("$V,"):
            seq = int(m.split("*")[0].split(",")[6])
            break
    if seq is None:
        print("No llegaron lecturas validas en 30 s. ¿Esta corriendo el nodo sensor?")
        return

    def enviar_y_ver(nombre, t, esperado):
        print(f"\n[{nombre}] envio: {t}")
        cli.publicar(TOPICO_CMD, t)
        for m in cli.mensajes(8):
            if m.startswith(("$OK", "$ER")):
                cuerpo = m.split("*")[0]
                resultado = "PASA" if cuerpo == esperado else "FALLA"
                print(f"  respuesta: {m}  (esperado {esperado}) -> {resultado}")
                return cuerpo == esperado
        print("  sin respuesta -> FALLA")
        return False

    seq += 1
    valido = trama("#", f"{seq},U,500")
    pruebas = [
        ("Comando valido", valido, "$OK,U500"),
        ("Repeticion del mismo comando", valido, "$ER,REPETIDO"),
        ("Checksum alterado", trama("#", f"{seq + 1},U,350", cs_ok=False), "$ER,CHECKSUM"),
        ("Firma falsa (atacante sin clave)", trama("#", f"{seq + 2},M,0", clave_ok=False), "$ER,FIRMA"),
        ("Umbral fuera de rango", trama("#", f"{seq + 3},U,-500"), "$ER,RANGO"),
        ("Umbral no numerico", trama("#", f"{seq + 4},U,abc"), "$ER,RANGO"),
        ("Comando fuera de lista blanca", trama("#", f"{seq + 5},RESET"), "$ER,COMANDO"),
        ("Mensaje demasiado largo", "#" + "X" * 100, "$ER,LARGO"),
        ("Volver a umbral 300", trama("#", f"{seq + 6},U,300"), "$OK,U300"),
    ]
    resultados = [enviar_y_ver(*p) for p in pruebas]
    print(f"\nResumen: {sum(resultados)}/{len(resultados)} pruebas pasan")


if __name__ == "__main__":
    main()
