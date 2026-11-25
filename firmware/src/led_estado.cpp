#include "led_estado.h"

void EstadoLed::aplica(const Comando &c, uint32_t agora_ms)
{
    if (c.acao != ACAO_DESLIGAR) {        /* so o DESLIGAR nao muda a cor */
        cor_r = c.r;
        cor_g = c.g;
        cor_b = c.b;
    }
    pisca_ms = 0;                         /* qualquer comando novo para o pisca */

    switch (c.acao) {
    case ACAO_LIGAR:    ligado = true;    break;
    case ACAO_DESLIGAR: ligado = false;   break;
    case ACAO_TOGGLE:   ligado = !ligado; break;
    case ACAO_PISCA_1S: inicia_pisca(1000, agora_ms); break;
    case ACAO_PISCA_2S: inicia_pisca(2000, agora_ms); break;
    }
}

void EstadoLed::inicia_pisca(uint32_t periodo_ms, uint32_t agora_ms)
{
    ligado = true;                        /* comeca aceso */
    pisca_ms = periodo_ms;
    t_ultima_troca = agora_ms;
}

void EstadoLed::atualiza(uint32_t agora_ms)
{
    if (pisca_ms > 0 && agora_ms - t_ultima_troca >= pisca_ms) {
        ligado = !ligado;
        t_ultima_troca += pisca_ms;       /* soma o periodo: o erro nao acumula */
    }
}

static void conta(Contadores &cont, Resultado r)
{
    switch (r) {
    case PACOTE_OK:    cont.ok++;           break;
    case ERRO_ID:      cont.erro_id++;      break;
    case ERRO_CHECK:   cont.erro_check++;   break;
    case ERRO_ACAO:    cont.erro_acao++;    break;
    case ERRO_TIMEOUT: cont.erro_timeout++; break;
    case INCOMPLETO:                        break;
    }
}

int processa_uart(LeituraUart ler, Receptor &rx, EstadoLed &led,
                  Contadores &cont, uint32_t agora_ms, AvisoPacote aviso)
{
    int lidos = 0;
    uint8_t byte;
    while (ler(&byte) == 0) {             /* sem dado (ou RX desligado): sai na hora */
        lidos++;
        Resultado r = rx.recebe(byte, agora_ms);
        conta(cont, r);
        if (r == PACOTE_OK) led.aplica(rx.comando(), agora_ms);
        if (r != INCOMPLETO && aviso) aviso(r, rx.comando());
    }
    Resultado t = rx.verifica_timeout(agora_ms);
    conta(cont, t);
    if (t == ERRO_TIMEOUT && aviso) aviso(t, rx.comando());
    return lidos;
}
