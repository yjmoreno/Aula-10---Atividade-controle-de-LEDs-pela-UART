/* ====================================================
 * Protocolo de 6 bytes para controlar o LED RGB pela UART
 *
 *   | ID | R | G | B | ACAO | CHECK |
 *
 * CHECK = XOR dos 5 primeiros bytes (ou CRC-8, no bonus).
 * Este arquivo nao usa nada do Zephyr: roda na placa e no PC,
 * por isso da para testar a logica no computador (TDD).
 * ==================================================== */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define PACOTE_TAM 6

/* Codigos de ACAO */
#define ACAO_LIGAR     0x01
#define ACAO_DESLIGAR  0x02
#define ACAO_TOGGLE    0x03
#define ACAO_PISCA_1S  0x04
#define ACAO_PISCA_2S  0x05

/* O que aconteceu ao receber um byte */
enum Resultado {
    INCOMPLETO,     /* ainda faltam bytes */
    PACOTE_OK,      /* pacote valido, comando() tem os dados */
    ERRO_ID,        /* pacote de outro ID: os 6 bytes sao descartados */
    ERRO_CHECK,     /* CHECK nao bate: pacote descartado */
    ERRO_ACAO,      /* codigo de acao desconhecido: pacote descartado */
    ERRO_TIMEOUT,   /* pacote ficou pela metade: bytes descartados */
};

struct Comando {
    uint8_t r, g, b;
    uint8_t acao;
};

uint8_t check_xor(const uint8_t *dados, size_t n);
uint8_t check_crc8(const uint8_t *dados, size_t n);

/* Nomes para o log (ex.: "ERRO_CHECK", "PISCA_1S") */
const char *nome_resultado(Resultado r);
const char *nome_acao(uint8_t acao);

class Receptor {
public:
    Receptor(uint8_t meu_id, uint32_t timeout_ms, bool usa_crc8 = false);

    /* Entrega um byte recebido no instante agora_ms */
    Resultado recebe(uint8_t byte, uint32_t agora_ms);

    /* Chamar de tempos em tempos: descarta pacote parado ha mais de timeout_ms */
    Resultado verifica_timeout(uint32_t agora_ms);

    Comando comando() const { return ultimo; }
    int pendentes() const { return n; }

private:
    bool passou_do_prazo(uint32_t agora_ms) const;
    Resultado valida_pacote();

    uint8_t id;
    uint32_t timeout;
    bool crc8;
    uint8_t buf[PACOTE_TAM];
    int n;
    uint32_t t_ultimo_byte;
    Comando ultimo;
};
