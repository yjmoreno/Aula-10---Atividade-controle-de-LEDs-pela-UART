/* Placa falsa: o mesmo protocolo e LED da placa, mas lendo stdin e
 * escrevendo o log em stdout. Serve para testar o envia_pacote.py
 * sem a placa, ligando os dois por uma porta serial virtual (pty). */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <thread>
#include "protocolo.h"
#include "led_estado.h"

static auto t0 = std::chrono::steady_clock::now();
static uint32_t agora() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
}
static int le_stdin(uint8_t *b) { return read(0, b, 1) == 1 ? 0 : -1; }
static void avisa(Resultado r, const Comando &c) {
    if (r == PACOTE_OK) printf("%u;RX;OK;%s;%u;%u;%u\n", agora(), nome_acao(c.acao), c.r, c.g, c.b);
    else printf("%u;RX;%s\n", agora(), nome_resultado(r));
    fflush(stdout);
}
int main() {
    fcntl(0, F_SETFL, O_NONBLOCK);
    Receptor rx(0x01, 50); EstadoLed led; Contadores cont = {};
    bool antes = false;
    printf("%u;INFO;placa falsa\n", agora()); fflush(stdout);
    while (1) {
        uint32_t t = agora();
        processa_uart(le_stdin, rx, led, cont, t, avisa);
        led.atualiza(t);
        if (led.aceso() != antes) {
            if (led.aceso()) printf("%u;LED;ON;%u;%u;%u\n", t, led.r(), led.g(), led.b());
            else printf("%u;LED;OFF\n", t);
            fflush(stdout); antes = led.aceso();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
