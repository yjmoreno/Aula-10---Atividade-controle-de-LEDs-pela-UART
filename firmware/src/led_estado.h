/* ====================================================
 * Estado do LED RGB: o que cada acao faz e o pisca.
 * Tambem sem Zephyr, para testar no computador.
 * ==================================================== */
#pragma once
#include <stdint.h>
#include "protocolo.h"

class EstadoLed {
public:
    /* Aplica um comando valido recebido no instante agora_ms */
    void aplica(const Comando &c, uint32_t agora_ms);

    /* Chamar periodicamente: faz o pisca acontecer */
    void atualiza(uint32_t agora_ms);

    bool aceso() const { return ligado; }
    uint8_t r() const { return cor_r; }
    uint8_t g() const { return cor_g; }
    uint8_t b() const { return cor_b; }
    uint32_t periodo_pisca() const { return pisca_ms; }

private:
    void inicia_pisca(uint32_t periodo_ms, uint32_t agora_ms);

    bool ligado = false;
    uint8_t cor_r = 0, cor_g = 0, cor_b = 0;
    uint32_t pisca_ms = 0;        /* 0 = sem pisca */
    uint32_t t_ultima_troca = 0;
};

/* Funcao que le um byte da UART: devolve 0 se leu, ou negativo se nao tem
 * nada (igual ao uart_poll_in do Zephyr). Assim o teste usa uma UART falsa. */
typedef int (*LeituraUart)(uint8_t *byte);

struct Contadores {
    uint32_t ok, erro_id, erro_check, erro_acao, erro_timeout;
};

/* Chamada a cada pacote terminado (valido ou nao), para o log */
typedef void (*AvisoPacote)(Resultado r, const Comando &c);

/* Le todos os bytes disponiveis, alimenta o receptor e aplica os comandos
 * validos no LED. Retorna quantos bytes foram lidos. Nunca bloqueia. */
int processa_uart(LeituraUart ler, Receptor &rx, EstadoLed &led,
                  Contadores &cont, uint32_t agora_ms, AvisoPacote aviso = nullptr);
