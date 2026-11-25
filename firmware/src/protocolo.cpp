#include "protocolo.h"

/* Posicao de cada campo dentro do pacote */
enum { POS_ID = 0, POS_R, POS_G, POS_B, POS_ACAO, POS_CHECK };

uint8_t check_xor(const uint8_t *dados, size_t n)
{
    uint8_t x = 0;
    for (size_t i = 0; i < n; i++) x ^= dados[i];
    return x;
}

/* CRC-8 com polinomio x^8 + x^2 + x + 1 (0x07), valor inicial 0x00 */
uint8_t check_crc8(const uint8_t *dados, size_t n)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < n; i++) {
        crc ^= dados[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else            crc = crc << 1;
        }
    }
    return crc;
}

const char *nome_resultado(Resultado r)
{
    switch (r) {
    case INCOMPLETO:   return "INCOMPLETO";
    case PACOTE_OK:    return "OK";
    case ERRO_ID:      return "ERRO_ID";
    case ERRO_CHECK:   return "ERRO_CHECK";
    case ERRO_ACAO:    return "ERRO_ACAO";
    case ERRO_TIMEOUT: return "ERRO_TIMEOUT";
    }
    return "?";
}

const char *nome_acao(uint8_t acao)
{
    switch (acao) {
    case ACAO_LIGAR:    return "LIGAR";
    case ACAO_DESLIGAR: return "DESLIGAR";
    case ACAO_TOGGLE:   return "TOGGLE";
    case ACAO_PISCA_1S: return "PISCA_1S";
    case ACAO_PISCA_2S: return "PISCA_2S";
    }
    return "?";
}

static bool acao_valida(uint8_t a)
{
    return a >= ACAO_LIGAR && a <= ACAO_PISCA_2S;
}

Receptor::Receptor(uint8_t meu_id, uint32_t timeout_ms, bool usa_crc8)
{
    id = meu_id;
    timeout = timeout_ms;
    crc8 = usa_crc8;
    n = 0;
    t_ultimo_byte = 0;
    ultimo.r = ultimo.g = ultimo.b = ultimo.acao = 0;
}

bool Receptor::passou_do_prazo(uint32_t agora_ms) const
{
    return n > 0 && agora_ms - t_ultimo_byte > timeout;
}

Resultado Receptor::recebe(uint8_t byte, uint32_t agora_ms)
{
    if (passou_do_prazo(agora_ms)) n = 0;     /* pacote velho, pela metade */
    t_ultimo_byte = agora_ms;

    /* Junta sempre os 6 bytes, mesmo de pacote de outro ID: assim um byte
     * do meio de um pacote alheio nao vira o inicio de um pacote falso.
     * Se a linha desalinhar (lixo), o silencio maior que o timeout realinha. */
    buf[n++] = byte;
    if (n < PACOTE_TAM) return INCOMPLETO;

    n = 0;                                    /* pacote completo: valida */
    return valida_pacote();
}

Resultado Receptor::valida_pacote()
{
    if (buf[POS_ID] != id) return ERRO_ID;   /* pacote de outro dispositivo */

    uint8_t esperado;
    if (crc8) esperado = check_crc8(buf, 5);   /* bonus */
    else      esperado = check_xor(buf, 5);    /* XOR dos 5 primeiros bytes */
    if (buf[POS_CHECK] != esperado) return ERRO_CHECK;
    if (!acao_valida(buf[POS_ACAO])) return ERRO_ACAO;

    ultimo.r = buf[POS_R];
    ultimo.g = buf[POS_G];
    ultimo.b = buf[POS_B];
    ultimo.acao = buf[POS_ACAO];
    return PACOTE_OK;
}

Resultado Receptor::verifica_timeout(uint32_t agora_ms)
{
    if (!passou_do_prazo(agora_ms)) return INCOMPLETO;
    n = 0;
    return ERRO_TIMEOUT;
}
