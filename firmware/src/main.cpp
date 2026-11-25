/* ====================================================
 * PSI Microcontroladores 2 - Controle de LEDs pela UART
 * FRDM-KL25Z + Zephyr
 *
 * Pacote de 6 bytes pela UART0 (USB da placa), 115200 8N1:
 *   | ID | R | G | B | ACAO | CHECK |
 *
 * A interrupcao de RX coloca cada byte numa fila (k_msgq).
 * A thread principal tira os bytes da fila, monta o pacote
 * (protocolo.cpp), aplica a acao no LED (led_estado.cpp) e
 * acende o LED RGB da placa.
 *
 * Log pela mesma serial:  <tempo_ms>;RX;<resultado>[;acao;r;g;b]
 *                         <tempo_ms>;LED;ON|OFF[;r;g;b]
 * ==================================================== */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#include "protocolo.h"
#include "led_estado.h"

#define MEU_ID      0x01    /* ID desta placa */
#define TIMEOUT_MS  50      /* intervalo maximo entre bytes do mesmo pacote */
#define USA_CRC8    false   /* bonus: true = CHECK por CRC-8 em vez de XOR */

/* ---------- UART: interrupcao -> fila ---------- */
static const device *const uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
/* 512 bytes: no teste P09 (50 pacotes seguidos) a fila de 64 enchia, porque
 * os bytes chegam mais rapido (1 pacote a cada ~0,5 ms) do que a placa escreve
 * o log de cada pacote (~2 ms). Com 512 cabem os 300 bytes da rajada. */
K_MSGQ_DEFINE(fila_rx, sizeof(uint8_t), 512, 1);
static volatile uint32_t perdidos = 0;   /* bytes que nao couberam na fila */

static void uart_isr(const device *dev, void *)
{
    if (!uart_irq_update(dev) || !uart_irq_rx_ready(dev)) return;
    uint8_t c;
    while (uart_fifo_read(dev, &c, 1) == 1) {
        if (k_msgq_put(&fila_rx, &c, K_NO_WAIT) != 0) perdidos++;
    }
}

/* Leitura sem bloquear: igual ao que o teste faz com a UART falsa */
static int le_fila(uint8_t *byte)
{
    return k_msgq_get(&fila_rx, byte, K_NO_WAIT);
}

/* ---------- LED RGB da placa ---------- */
static const gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
static const gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/* Sem PWM na KL25Z neste Zephyr: cada cor acende se o valor for > 0 */
static void mostra_led(const EstadoLed &led)
{
    static bool estava_aceso = false;
    gpio_pin_set_dt(&led_r, led.aceso() && led.r() > 0);
    gpio_pin_set_dt(&led_g, led.aceso() && led.g() > 0);
    gpio_pin_set_dt(&led_b, led.aceso() && led.b() > 0);

    if (led.aceso() != estava_aceso) {
        if (led.aceso())
            printk("%u;LED;ON;%u;%u;%u\n", k_uptime_get_32(), led.r(), led.g(), led.b());
        else
            printk("%u;LED;OFF\n", k_uptime_get_32());
        estava_aceso = led.aceso();
    }
}

/* ---------- log de cada pacote ---------- */
static void avisa(Resultado r, const Comando &c)
{
    if (r == PACOTE_OK)
        printk("%u;RX;OK;%s;%u;%u;%u\n", k_uptime_get_32(), nome_acao(c.acao), c.r, c.g, c.b);
    else
        printk("%u;RX;%s\n", k_uptime_get_32(), nome_resultado(r));
}

int main(void)
{
    gpio_pin_configure_dt(&led_r, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_b, GPIO_OUTPUT_INACTIVE);

    if (!device_is_ready(uart)) {
        printk("UART nao esta pronta\n");
        return 0;
    }
    uart_irq_callback_user_data_set(uart, uart_isr, NULL);
    uart_irq_rx_enable(uart);

    printk("%u;INFO;Controle de LED pela UART. ID=0x%02X, CHECK=%s, timeout=%d ms\n",
           k_uptime_get_32(), MEU_ID, USA_CRC8 ? "CRC-8" : "XOR", TIMEOUT_MS);

    Receptor rx(MEU_ID, TIMEOUT_MS, USA_CRC8);
    EstadoLed led;
    Contadores cont = {};
    uint32_t perdidos_avisados = 0;

    while (1) {
        uint32_t agora = k_uptime_get_32();
        processa_uart(le_fila, rx, led, cont, agora, avisa);
        if (perdidos != perdidos_avisados) {        /* fila cheia: avisa no log */
            perdidos_avisados = perdidos;
            printk("%u;RX;BYTES_PERDIDOS;%u\n", agora, perdidos_avisados);
        }
        led.atualiza(agora);
        mostra_led(led);
        k_msleep(5);
    }
    return 0;
}
