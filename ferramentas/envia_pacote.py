"""
Envia pacotes do protocolo | ID | R | G | B | ACAO | CHECK | para a placa
e (opcional) roda o roteiro de testes na placa, conferindo o log.

Precisa do pyserial:  pip install pyserial
No Mac a porta e algo como /dev/cu.usbmodem1102  (ls /dev/cu.usbmodem*)
Feche o monitor do PlatformIO antes, porque ele ocupa a porta.

Exemplos:
  python envia_pacote.py PORTA ligar 255 0 0        # vermelho
  python envia_pacote.py PORTA pisca1 0 0 255       # azul piscando 1 s
  python envia_pacote.py PORTA ligar 0 255 0 --id 2 # ID errado
  python envia_pacote.py PORTA ligar 0 255 0 --estraga-check
  python envia_pacote.py PORTA ligar 0 255 0 --so 3 # manda so 3 bytes (truncado)
  python envia_pacote.py PORTA --roteiro --saida ../docs/evidence/placa/roteiro.txt
"""
import argparse
import sys
import time

ACOES = {"ligar": 0x01, "desligar": 0x02, "toggle": 0x03, "pisca1": 0x04, "pisca2": 0x05}


def check_xor(dados):
    x = 0
    for b in dados:
        x ^= b
    return x


def check_crc8(dados):
    crc = 0
    for b in dados:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def monta(id_, r, g, b, acao, crc8=False):
    corpo = bytes([id_, r, g, b, acao])
    return corpo + bytes([check_crc8(corpo) if crc8 else check_xor(corpo)])


class Placa:
    """Conversa com a placa e guarda todas as linhas do log."""

    def __init__(self, porta):
        import serial
        self.s = serial.Serial(porta, 115200, timeout=0.05)   # 8N1 e o padrao
        self.log = []
        time.sleep(0.2)
        self.s.reset_input_buffer()

    def envia(self, dados):
        self.s.write(dados)
        self.s.flush()
        self.log.append(">> " + dados.hex(" ").upper())

    def le_por(self, segundos):
        """Le as linhas que chegarem durante 'segundos' e devolve so elas."""
        novas, fim, resto = [], time.monotonic() + segundos, b""
        while time.monotonic() < fim:
            resto += self.s.read(256)
            while b"\n" in resto:
                linha, resto = resto.split(b"\n", 1)
                texto = linha.decode(errors="ignore").strip()
                if texto:
                    novas.append(texto)
                    self.log.append(texto)
        return novas


def campos(linhas, tipo):
    """Pega as linhas de um tipo (RX ou LED) ja quebradas em campos."""
    out = []
    for l in linhas:
        p = l.split(";")
        if len(p) >= 3 and p[1] == tipo and p[0].isdigit():
            out.append(p)
    return out


def roteiro(placa, crc8=False):
    """Casos de teste na placa. Cada um devolve (id, descricao, passou, detalhe)."""
    res = []

    def caso(cid, desc, passou, det):
        res.append((cid, desc, passou, det))
        print(f"[{'  OK  ' if passou else 'FALHOU'}] {cid} {desc}  ({det})")

    P = lambda r, g, b, a, i=0x01: monta(i, r, g, b, a, crc8)

    placa.envia(P(0, 0, 0, 0x02)); placa.le_por(0.3)           # comeca apagado

    placa.envia(P(255, 0, 0, 0x01)); l = placa.le_por(0.3)
    rx, led = campos(l, "RX"), campos(l, "LED")
    caso("P01", "LIGAR vermelho", rx and rx[0][2:] == ["OK", "LIGAR", "255", "0", "0"]
         and led and led[0][2] == "ON", " | ".join(l))

    placa.envia(P(0, 0, 0, 0x02)); l = placa.le_por(0.3)
    caso("P02", "DESLIGAR", any(x[2] == "OFF" for x in campos(l, "LED")), " | ".join(l))

    placa.envia(P(0, 255, 0, 0x03)); l1 = placa.le_por(0.3)
    placa.envia(P(0, 255, 0, 0x03)); l2 = placa.le_por(0.3)
    caso("P03", "TOGGLE duas vezes (acende e apaga)",
         any(x[2] == "ON" for x in campos(l1, "LED")) and any(x[2] == "OFF" for x in campos(l2, "LED")),
         " | ".join(l1 + l2))

    placa.envia(P(255, 255, 255, 0x01, i=0x02)); l = placa.le_por(0.3)
    caso("P04", "ID errado e recusado", [x[2] for x in campos(l, "RX")] == ["ERRO_ID"]
         and not campos(l, "LED"), " | ".join(l))

    ruim = bytearray(P(255, 0, 0, 0x01)); ruim[5] ^= 0xFF
    placa.envia(bytes(ruim)); l = placa.le_por(0.3)
    caso("P05", "CHECK errado e recusado", [x[2] for x in campos(l, "RX")] == ["ERRO_CHECK"]
         and not campos(l, "LED"), " | ".join(l))

    placa.envia(P(255, 0, 0, 0x09)); l = placa.le_por(0.3)
    caso("P06", "acao invalida e recusada", [x[2] for x in campos(l, "RX")] == ["ERRO_ACAO"],
         " | ".join(l))

    placa.envia(P(0, 0, 255, 0x01)[:3]); l = placa.le_por(0.4)
    caso("P07", "pacote truncado (3 bytes) cai no timeout",
         [x[2] for x in campos(l, "RX")] == ["ERRO_TIMEOUT"], " | ".join(l))

    placa.envia(bytes([0x55, 0xAA])); time.sleep(0.1)       # lixo e depois silencio
    placa.envia(P(0, 0, 255, 0x01)); l = placa.le_por(0.3)
    caso("P08", "lixo, silencio e pacote valido: ressincroniza",
         [x[2] for x in campos(l, "RX")] == ["ERRO_TIMEOUT", "OK"], " | ".join(l))

    rajada = b"".join(P(i % 256, 0, 0, 0x01) for i in range(50))
    placa.envia(rajada); l = placa.le_por(1.0)
    oks = sum(1 for x in campos(l, "RX") if x[2] == "OK")
    caso("P09", "buffer grande: 50 pacotes seguidos (300 bytes)", oks == 50, f"{oks} de 50 OK")

    placa.envia(P(0, 0, 0, 0x02)); placa.le_por(0.3)
    l = placa.le_por(2.0)
    caso("P10", "entrada vazia: 2 s sem enviar nada", not campos(l, "RX"),
         f"{len(campos(l, 'RX'))} linhas RX")

    for cid, acao, periodo in (("P11", 0x04, 1000), ("P12", 0x05, 2000)):
        placa.envia(P(255, 0, 255, acao)); l = placa.le_por(periodo * 4.5 / 1000)
        t = [int(x[0]) for x in campos(l, "LED")]
        dif = [b - a for a, b in zip(t, t[1:])]
        ok = len(dif) >= 3 and all(abs(d - periodo) <= 30 for d in dif)
        caso(cid, f"PISCA {periodo // 1000} s", ok, f"intervalos {dif} ms")

    placa.envia(P(0, 0, 0, 0x02)); placa.le_por(0.3)
    return res


def main():
    ap = argparse.ArgumentParser(description="Envia pacotes para o LED pela UART")
    ap.add_argument("porta")
    ap.add_argument("acao", nargs="?", choices=list(ACOES))
    ap.add_argument("cor", nargs="*", type=int, help="R G B (0 a 255)")
    ap.add_argument("--id", type=lambda x: int(x, 0), default=0x01)
    ap.add_argument("--crc8", action="store_true", help="CHECK por CRC-8 (bonus)")
    ap.add_argument("--estraga-check", action="store_true")
    ap.add_argument("--so", type=int, default=6, help="manda so os N primeiros bytes")
    ap.add_argument("--roteiro", action="store_true", help="roda os testes na placa")
    ap.add_argument("--saida", help="arquivo para salvar o log")
    a = ap.parse_args()

    placa = Placa(a.porta)
    if a.roteiro:
        res = roteiro(placa, a.crc8)
        n_ok = sum(1 for r in res if r[2])
        print(f"\n{n_ok} de {len(res)} casos passaram")
    else:
        if not a.acao or len(a.cor) != 3:
            ap.error("informe a acao e R G B, ou use --roteiro")
        pac = bytearray(monta(a.id, *a.cor, ACOES[a.acao], a.crc8))
        if a.estraga_check:
            pac[5] ^= 0xFF
        placa.envia(bytes(pac[:a.so]))
        print(">>", bytes(pac[:a.so]).hex(" ").upper())
        for linha in placa.le_por(0.5):
            print(linha)
        res = None

    if a.saida:
        with open(a.saida, "w") as f:
            f.write("\n".join(placa.log) + "\n")
            if res:
                f.write(f"\n{sum(1 for r in res if r[2])} de {len(res)} casos passaram\n")
        print("log salvo em", a.saida)
    sys.exit(0 if res is None or all(r[2] for r in res) else 1)


if __name__ == "__main__":
    main()
