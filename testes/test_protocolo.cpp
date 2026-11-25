/* ====================================================
 * Testes do protocolo UART e do LED (rodam no computador)
 *
 * Compilar e rodar:   ./roda_testes.sh
 * Cada TESTE e independente. CHECA mostra a linha que falhou.
 * ==================================================== */
#include <stdio.h>
#include <string.h>
#include "protocolo.h"
#include "led_estado.h"

/* ---------- mini framework de teste ---------- */
static int total = 0, falhas = 0, falhou_este = 0;

#define CHECA(cond) do { if (!(cond)) { \
        printf("    FALHOU linha %d: %s\n", __LINE__, #cond); falhou_este = 1; } } while (0)

#define TESTE(nome) static void nome()
#define RODA(nome, descricao) do { falhou_este = 0; total++; nome(); \
        if (falhou_este) falhas++; \
        printf("[%s] %s\n", falhou_este ? "FALHOU" : "  OK  ", descricao); } while (0)

/* ---------- ajudantes ---------- */
static const uint8_t MEU_ID = 0x01;
static const uint32_t TIMEOUT = 50;   /* ms */

/* Monta um pacote com o CHECK certo (XOR) */
static void monta(uint8_t *p, uint8_t id, uint8_t r, uint8_t g, uint8_t b, uint8_t acao)
{
    p[0] = id; p[1] = r; p[2] = g; p[3] = b; p[4] = acao;
    p[5] = p[0] ^ p[1] ^ p[2] ^ p[3] ^ p[4];
}

/* Entrega n bytes, 1 ms entre eles, e devolve o resultado do ultimo */
static Resultado envia(Receptor &rx, const uint8_t *p, int n, uint32_t &t)
{
    Resultado res = INCOMPLETO;
    for (int i = 0; i < n; i++) res = rx.recebe(p[i], t++);
    return res;
}

/* ================= protocolo ================= */

TESTE(t01_entrada_valida_simples)
{
    Receptor rx(MEU_ID, TIMEOUT);
    uint8_t p[6]; monta(p, MEU_ID, 0xFF, 0x00, 0x00, ACAO_LIGAR);
    uint32_t t = 0;
    for (int i = 0; i < 5; i++) CHECA(rx.recebe(p[i], t++) == INCOMPLETO);
    CHECA(rx.recebe(p[5], t++) == PACOTE_OK);
    Comando c = rx.comando();
    CHECA(c.r == 0xFF && c.g == 0x00 && c.b == 0x00 && c.acao == ACAO_LIGAR);
    CHECA(rx.pendentes() == 0);
}

TESTE(t02_check_xor_valor_conhecido)
{
    const uint8_t d[5] = {0x01, 0xFF, 0x00, 0x00, 0x01};
    CHECA(check_xor(d, 5) == 0xFF);
    const uint8_t z[5] = {0, 0, 0, 0, 0};
    CHECA(check_xor(z, 5) == 0x00);
}

TESTE(t03_todas_as_acoes_validas)
{
    Receptor rx(MEU_ID, TIMEOUT);
    uint32_t t = 0;
    for (uint8_t a = ACAO_LIGAR; a <= ACAO_PISCA_2S; a++) {
        uint8_t p[6]; monta(p, MEU_ID, 1, 2, 3, a);
        CHECA(envia(rx, p, 6, t) == PACOTE_OK);
        CHECA(rx.comando().acao == a);
    }
}

TESTE(t04_entrada_vazia)
{
    Receptor rx(MEU_ID, TIMEOUT);
    CHECA(rx.verifica_timeout(0) == INCOMPLETO);
    CHECA(rx.verifica_timeout(100000) == INCOMPLETO);   /* muito tempo sem nada */
    CHECA(rx.pendentes() == 0);
}

TESTE(t05_id_errado)
{
    /* Pacote de outro dispositivo: os 6 bytes sao pulados e o erro sai no fim */
    Receptor rx(MEU_ID, TIMEOUT);
    uint8_t p[6]; monta(p, 0x02, 0xFF, 0xFF, 0xFF, ACAO_LIGAR);
    uint32_t t = 0;
    for (int i = 0; i < 5; i++) CHECA(rx.recebe(p[i], t++) == INCOMPLETO);
    CHECA(rx.recebe(p[5], t++) == ERRO_ID);
    CHECA(rx.pendentes() == 0);
    CHECA(rx.comando().acao == 0);
}

TESTE(t06_check_errado)
{
    Receptor rx(MEU_ID, TIMEOUT);
    uint8_t p[6]; monta(p, MEU_ID, 0x10, 0x20, 0x30, ACAO_LIGAR);
    p[5] ^= 0x01;                               /* estraga 1 bit do CHECK */
    uint32_t t = 0;
    CHECA(envia(rx, p, 6, t) == ERRO_CHECK);
    CHECA(rx.pendentes() == 0);
    CHECA(rx.comando().acao == 0);              /* nenhum comando aceito */
}

TESTE(t07_acao_invalida)
{
    Receptor rx(MEU_ID, TIMEOUT);
    uint32_t t = 0;
    uint8_t p[6];
    monta(p, MEU_ID, 1, 1, 1, 0x00);
    CHECA(envia(rx, p, 6, t) == ERRO_ACAO);
    monta(p, MEU_ID, 1, 1, 1, 0x06);
    CHECA(envia(rx, p, 6, t) == ERRO_ACAO);
    CHECA(rx.pendentes() == 0);
}

TESTE(t08_transmissao_interrompida_timeout)
{
    Receptor rx(MEU_ID, TIMEOUT);
    uint8_t p[6]; monta(p, MEU_ID, 0, 0xFF, 0, ACAO_LIGAR);
    uint32_t t = 0;
    envia(rx, p, 3, t);                         /* so metade do pacote */
    CHECA(rx.pendentes() == 3);
    CHECA(rx.verifica_timeout(t + TIMEOUT / 2) == INCOMPLETO);   /* ainda dentro do prazo */
    CHECA(rx.verifica_timeout(t + TIMEOUT + 1) == ERRO_TIMEOUT);
    CHECA(rx.pendentes() == 0);
    t += TIMEOUT + 10;
    CHECA(envia(rx, p, 6, t) == PACOTE_OK);     /* o proximo pacote funciona */
}

TESTE(t09_interrompida_detectada_no_proximo_byte)
{
    /* Sem chamar verifica_timeout: o pacote velho tem que ser descartado
     * quando chega um byte depois de um intervalo maior que o timeout */
    Receptor rx(MEU_ID, TIMEOUT);
    uint8_t p[6]; monta(p, MEU_ID, 0, 0, 0xFF, ACAO_LIGAR);
    uint32_t t = 0;
    envia(rx, p, 4, t);
    t += TIMEOUT + 5;
    CHECA(envia(rx, p, 6, t) == PACOTE_OK);
    CHECA(rx.comando().b == 0xFF);
}

TESTE(t10_lixo_antes_do_pacote_ressincroniza)
{
    /* Lixo na linha: o receptor volta a se alinhar depois de um silencio
     * maior que o timeout (e assim que o remetente separa os pacotes) */
    Receptor rx(MEU_ID, TIMEOUT);
    uint32_t t = 0;
    rx.recebe(0x55, t++);
    rx.recebe(0xAA, t++);
    t += TIMEOUT + 1;
    uint8_t p[6]; monta(p, MEU_ID, 7, 8, 9, ACAO_TOGGLE);
    CHECA(envia(rx, p, 6, t) == PACOTE_OK);
}

TESTE(t16_outro_id_nao_cria_pacote_falso)
{
    /* Bug achado pelo roteiro na placa falsa: o byte de ACAO 0x01 de um
     * pacote de outro ID era confundido com o inicio de um pacote nosso,
     * e o pacote verdadeiro que vinha logo depois era perdido */
    Receptor rx(MEU_ID, TIMEOUT);
    uint8_t outro[6]; monta(outro, 0x02, 0xFF, 0xFF, 0xFF, ACAO_LIGAR);
    uint8_t nosso[6]; monta(nosso, MEU_ID, 0, 0xFF, 0, ACAO_LIGAR);
    uint32_t t = 0;
    CHECA(envia(rx, outro, 6, t) == ERRO_ID);
    CHECA(envia(rx, nosso, 6, t) == PACOTE_OK);   /* chega 1 ms depois */
    CHECA(rx.comando().g == 0xFF);
}

TESTE(t11_buffer_grande_pacotes_seguidos)
{
    Receptor rx(MEU_ID, TIMEOUT);
    uint32_t t = 0;
    int ok = 0, maior = 0;
    for (int i = 0; i < 1000; i++) {
        uint8_t p[6]; monta(p, MEU_ID, (uint8_t)i, (uint8_t)(i * 3), (uint8_t)(i * 7),
                            (uint8_t)(1 + i % 5));
        for (int k = 0; k < 6; k++) {
            if (rx.recebe(p[k], t++) == PACOTE_OK) ok++;
            if (rx.pendentes() > maior) maior = rx.pendentes();
        }
    }
    CHECA(ok == 1000);
    CHECA(maior <= PACOTE_TAM - 1);
}

TESTE(t12_buffer_grande_de_lixo)
{
    Receptor rx(MEU_ID, TIMEOUT);
    uint32_t t = 0, semente = 12345;
    int maior = 0;
    for (int i = 0; i < 10000; i++) {
        semente = semente * 1103515245u + 12345u;     /* gerador simples */
        rx.recebe((uint8_t)(semente >> 16), t++);
        if (rx.pendentes() > maior) maior = rx.pendentes();
    }
    CHECA(maior <= PACOTE_TAM - 1);                    /* nunca estoura o buffer */
    t += TIMEOUT + 1;
    uint8_t p[6]; monta(p, MEU_ID, 1, 2, 3, ACAO_LIGAR);
    CHECA(envia(rx, p, 6, t) == PACOTE_OK);            /* continua funcionando */
}

TESTE(t13_intervalo_igual_ao_timeout_nao_descarta)
{
    Receptor rx(MEU_ID, TIMEOUT);
    uint8_t p[6]; monta(p, MEU_ID, 1, 2, 3, ACAO_LIGAR);
    uint32_t t = 0;
    for (int i = 0; i < 6; i++) { CHECA(rx.recebe(p[i], t) != ERRO_TIMEOUT); t += TIMEOUT; }
    CHECA(rx.comando().acao == ACAO_LIGAR);
}

/* ================= bonus: CRC-8 ================= */

TESTE(t14_crc8_valor_de_referencia)
{
    /* CRC-8 (polinomio 0x07, valor inicial 0x00): "123456789" -> 0xF4 */
    const uint8_t d[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECA(check_crc8(d, 9) == 0xF4);
}

TESTE(t15_receptor_com_crc8)
{
    Receptor rx(MEU_ID, TIMEOUT, true);
    uint8_t p[6]; monta(p, MEU_ID, 0x12, 0x34, 0x56, ACAO_LIGAR);
    uint32_t t = 0;
    if (check_crc8(p, 5) != p[5]) {             /* com XOR tem que ser recusado */
        CHECA(envia(rx, p, 6, t) == ERRO_CHECK);
    }
    p[5] = check_crc8(p, 5);
    CHECA(envia(rx, p, 6, t) == PACOTE_OK);
}

/* ================= LED ================= */

static Comando cmd(uint8_t r, uint8_t g, uint8_t b, uint8_t acao)
{
    Comando c; c.r = r; c.g = g; c.b = b; c.acao = acao; return c;
}

TESTE(t20_ligar)
{
    EstadoLed led;
    led.aplica(cmd(0, 0xFF, 0, ACAO_LIGAR), 0);
    CHECA(led.aceso());
    CHECA(led.r() == 0 && led.g() == 0xFF && led.b() == 0);
}

TESTE(t21_desligar)
{
    EstadoLed led;
    led.aplica(cmd(0xFF, 0, 0, ACAO_LIGAR), 0);
    led.aplica(cmd(0, 0, 0, ACAO_DESLIGAR), 10);
    CHECA(!led.aceso());
}

TESTE(t22_toggle)
{
    EstadoLed led;
    led.aplica(cmd(0, 0, 0xFF, ACAO_TOGGLE), 0);
    CHECA(led.aceso() && led.b() == 0xFF);
    led.aplica(cmd(0, 0, 0xFF, ACAO_TOGGLE), 10);
    CHECA(!led.aceso());
}

TESTE(t23_pisca_1s)
{
    EstadoLed led;
    led.aplica(cmd(0xFF, 0, 0, ACAO_PISCA_1S), 1000);
    CHECA(led.aceso());
    led.atualiza(1999); CHECA(led.aceso());     /* 1 s aceso */
    led.atualiza(2000); CHECA(!led.aceso());    /* 1 s apagado */
    led.atualiza(2999); CHECA(!led.aceso());
    led.atualiza(3000); CHECA(led.aceso());
}

TESTE(t24_pisca_2s)
{
    EstadoLed led;
    led.aplica(cmd(0xFF, 0, 0, ACAO_PISCA_2S), 0);
    led.atualiza(1999); CHECA(led.aceso());
    led.atualiza(2000); CHECA(!led.aceso());
    led.atualiza(4000); CHECA(led.aceso());
}

TESTE(t25_ligar_para_o_pisca)
{
    EstadoLed led;
    led.aplica(cmd(0xFF, 0, 0, ACAO_PISCA_1S), 0);
    led.aplica(cmd(0xFF, 0, 0, ACAO_LIGAR), 500);
    led.atualiza(5000);
    CHECA(led.aceso() && led.periodo_pisca() == 0);
}

/* ================= integracao com uma UART falsa ================= */

static uint8_t fila[64];
static int fila_ini = 0, fila_fim = 0;
static bool rx_desabilitado = false;

static void fila_poe(const uint8_t *d, int n) { for (int i = 0; i < n; i++) fila[fila_fim++] = d[i]; }
static void fila_limpa() { fila_ini = fila_fim = 0; rx_desabilitado = false; }

static int uart_falsa(uint8_t *byte)
{
    if (rx_desabilitado || fila_ini == fila_fim) return -1;
    *byte = fila[fila_ini++];
    return 0;
}

TESTE(t30_uart_pacote_valido_acende)
{
    fila_limpa();
    Receptor rx(MEU_ID, TIMEOUT); EstadoLed led; Contadores c = {};
    uint8_t p[6]; monta(p, MEU_ID, 0xFF, 0xFF, 0, ACAO_LIGAR);
    fila_poe(p, 6);
    CHECA(processa_uart(uart_falsa, rx, led, c, 0) == 6);
    CHECA(led.aceso() && c.ok == 1);
}

TESTE(t31_rx_desabilitado_nao_trava)
{
    fila_limpa();
    rx_desabilitado = true;
    Receptor rx(MEU_ID, TIMEOUT); EstadoLed led; Contadores c = {};
    uint8_t p[6]; monta(p, MEU_ID, 0xFF, 0, 0, ACAO_LIGAR);
    fila_poe(p, 6);                                    /* tem dado, mas o RX esta desligado */
    int lidos = 0;
    for (uint32_t t = 0; t < 1000; t++) lidos += processa_uart(uart_falsa, rx, led, c, t);
    CHECA(lidos == 0);
    CHECA(!led.aceso());
    CHECA(c.ok == 0 && c.erro_timeout == 0);
}

TESTE(t32_uart_invalido_depois_valido)
{
    fila_limpa();
    Receptor rx(MEU_ID, TIMEOUT); EstadoLed led; Contadores c = {};
    uint8_t ruim[6]; monta(ruim, MEU_ID, 1, 1, 1, ACAO_LIGAR); ruim[5] ^= 0xFF;
    uint8_t bom[6];  monta(bom, MEU_ID, 0, 0, 0xFF, ACAO_LIGAR);
    fila_poe(ruim, 6); fila_poe(bom, 6);
    processa_uart(uart_falsa, rx, led, c, 0);
    CHECA(c.erro_check == 1 && c.ok == 1);
    CHECA(led.aceso() && led.b() == 0xFF);
}

TESTE(t33_uart_timeout_sem_novos_bytes)
{
    fila_limpa();
    Receptor rx(MEU_ID, TIMEOUT); EstadoLed led; Contadores c = {};
    uint8_t p[6]; monta(p, MEU_ID, 1, 2, 3, ACAO_LIGAR);
    fila_poe(p, 2);
    processa_uart(uart_falsa, rx, led, c, 0);
    processa_uart(uart_falsa, rx, led, c, TIMEOUT + 10);   /* nada novo chegou */
    CHECA(c.erro_timeout == 1 && rx.pendentes() == 0);
}

int main()
{
    printf("== Protocolo ==\n");
    RODA(t01_entrada_valida_simples, "T01 entrada valida simples");
    RODA(t02_check_xor_valor_conhecido, "T02 CHECK por XOR");
    RODA(t03_todas_as_acoes_validas, "T03 as 5 acoes sao aceitas");
    RODA(t04_entrada_vazia, "T04 entrada vazia");
    RODA(t05_id_errado, "T05 ID errado");
    RODA(t06_check_errado, "T06 CHECK errado");
    RODA(t07_acao_invalida, "T07 acao invalida");
    RODA(t08_transmissao_interrompida_timeout, "T08 transmissao interrompida (timeout)");
    RODA(t09_interrompida_detectada_no_proximo_byte, "T09 interrompida, detectada no proximo byte");
    RODA(t10_lixo_antes_do_pacote_ressincroniza, "T10 lixo e depois silencio: ressincroniza");
    RODA(t11_buffer_grande_pacotes_seguidos, "T11 buffer grande: 1000 pacotes seguidos");
    RODA(t12_buffer_grande_de_lixo, "T12 buffer grande: 10000 bytes de lixo");
    RODA(t13_intervalo_igual_ao_timeout_nao_descarta, "T13 intervalo igual ao timeout");
    RODA(t16_outro_id_nao_cria_pacote_falso, "T16 pacote de outro ID nao cria pacote falso");
    printf("== Bonus CRC-8 ==\n");
    RODA(t14_crc8_valor_de_referencia, "T14 CRC-8 valor de referencia");
    RODA(t15_receptor_com_crc8, "T15 receptor no modo CRC-8");
    printf("== LED ==\n");
    RODA(t20_ligar, "T20 LIGAR");
    RODA(t21_desligar, "T21 DESLIGAR");
    RODA(t22_toggle, "T22 TOGGLE");
    RODA(t23_pisca_1s, "T23 PISCA 1 s");
    RODA(t24_pisca_2s, "T24 PISCA 2 s");
    RODA(t25_ligar_para_o_pisca, "T25 LIGAR para o pisca");
    printf("== Integracao com UART falsa ==\n");
    RODA(t30_uart_pacote_valido_acende, "T30 pacote pela UART acende o LED");
    RODA(t31_rx_desabilitado_nao_trava, "T31 RX desabilitado nao trava");
    RODA(t32_uart_invalido_depois_valido, "T32 invalido e depois valido");
    RODA(t33_uart_timeout_sem_novos_bytes, "T33 timeout sem bytes novos");

    printf("\n%d de %d testes passaram\n", total - falhas, total);
    return falhas ? 1 : 0;
}
