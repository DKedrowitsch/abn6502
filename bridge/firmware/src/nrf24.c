#include "nrf24.h"

#include "platform.h"

#include "ch32v20x.h"

#define NRF_CSN_PIN GPIO_Pin_4
#define NRF_CE_PIN GPIO_Pin_0
#define NRF_IRQ_PIN GPIO_Pin_1

#define NRF_CMD_R_REGISTER 0x00u
#define NRF_CMD_W_REGISTER 0x20u
#define NRF_CMD_ACTIVATE 0x50u
#define NRF_CMD_R_RX_PAYLOAD 0x61u
#define NRF_CMD_W_TX_PAYLOAD 0xa0u
#define NRF_CMD_FLUSH_TX 0xe1u
#define NRF_CMD_FLUSH_RX 0xe2u
#define NRF_CMD_NOP 0xffu

#define NRF_REG_CONFIG 0x00u
#define NRF_REG_EN_AA 0x01u
#define NRF_REG_EN_RXADDR 0x02u
#define NRF_REG_SETUP_AW 0x03u
#define NRF_REG_SETUP_RETR 0x04u
#define NRF_REG_RF_CH 0x05u
#define NRF_REG_RF_SETUP 0x06u
#define NRF_REG_STATUS 0x07u
#define NRF_REG_RX_ADDR_P0 0x0au
#define NRF_REG_TX_ADDR 0x10u
#define NRF_REG_DYNPD 0x1cu
#define NRF_REG_FEATURE 0x1du

#define NRF_STATUS_RX_DR 0x40u
#define NRF_STATUS_TX_DS 0x20u
#define NRF_STATUS_MAX_RT 0x10u

#if defined(__riscv_xw)
#define ABN_INTERRUPT __attribute__((interrupt("WCH-Interrupt-fast")))
#else
#define ABN_INTERRUPT __attribute__((interrupt("machine")))
#endif

static volatile uint8_t irq_event;

void EXTI1_IRQHandler(void) ABN_INTERRUPT;

void EXTI1_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line1) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line1);
        irq_event = 1;
    }
}

static void csn(int high)
{
    GPIO_WriteBit(GPIOA, NRF_CSN_PIN, high ? Bit_SET : Bit_RESET);
}

static void ce(int high)
{
    GPIO_WriteBit(GPIOB, NRF_CE_PIN, high ? Bit_SET : Bit_RESET);
}

static uint8_t spi_byte(uint8_t value)
{
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) {
    }
    SPI_I2S_SendData(SPI1, value);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET) {
    }
    return (uint8_t)SPI_I2S_ReceiveData(SPI1);
}

static uint8_t command(uint8_t value)
{
    uint8_t status;
    csn(0);
    status = spi_byte(value);
    csn(1);
    return status;
}

static uint8_t read_register(uint8_t reg)
{
    uint8_t value;
    csn(0);
    spi_byte(NRF_CMD_R_REGISTER | (reg & 0x1fu));
    value = spi_byte(NRF_CMD_NOP);
    csn(1);
    return value;
}

static void write_register(uint8_t reg, uint8_t value)
{
    csn(0);
    spi_byte(NRF_CMD_W_REGISTER | (reg & 0x1fu));
    spi_byte(value);
    csn(1);
}

static void write_register_bytes(uint8_t reg, const uint8_t *data, uint8_t length)
{
    csn(0);
    spi_byte(NRF_CMD_W_REGISTER | (reg & 0x1fu));
    while (length-- != 0u) {
        spi_byte(*data++);
    }
    csn(1);
}

static void clear_irqs(void)
{
    write_register(NRF_REG_STATUS, NRF_STATUS_RX_DR | NRF_STATUS_TX_DS | NRF_STATUS_MAX_RT);
}

int nrf24_init(void)
{
    static const uint8_t address[5] = {0xe7, 0xe7, 0xe7, 0xe7, 0xe7};
    GPIO_InitTypeDef gpio = {0};
    SPI_InitTypeDef spi = {0};
    EXTI_InitTypeDef exti = {0};
    NVIC_InitTypeDef nvic = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_AFIO | RCC_APB2Periph_SPI1, ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_6;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = NRF_CSN_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = NRF_CE_PIN;
    GPIO_Init(GPIOB, &gpio);
    gpio.GPIO_Pin = NRF_IRQ_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);
    csn(1);
    ce(0);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource1);
    EXTI_ClearITPendingBit(EXTI_Line1);
    exti.EXTI_Line = EXTI_Line1;
    exti.EXTI_Mode = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Falling;
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);
    nvic.NVIC_IRQChannel = EXTI1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority = 1;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_1Edge;
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 7;
    SPI_Init(SPI1, &spi);
    SPI_NSSInternalSoftwareConfig(SPI1, SPI_NSSInternalSoft_Set);
    SPI_Cmd(SPI1, ENABLE);

    platform_delay_ms(100);
    write_register(NRF_REG_CONFIG, 0x0c);
    write_register(NRF_REG_EN_AA, 0x01);
    write_register(NRF_REG_EN_RXADDR, 0x01);
    write_register(NRF_REG_SETUP_AW, 0x03);
    write_register(NRF_REG_SETUP_RETR, 0xff);
    write_register(NRF_REG_RF_CH, 0x02);
    write_register(NRF_REG_RF_SETUP, 0x0e);
    write_register_bytes(NRF_REG_RX_ADDR_P0, address, sizeof(address));
    write_register_bytes(NRF_REG_TX_ADDR, address, sizeof(address));
    write_register(NRF_REG_DYNPD, 0x01);
    write_register(NRF_REG_FEATURE, 0x04);
    if (read_register(NRF_REG_FEATURE) != 0x04) {
        csn(0);
        spi_byte(NRF_CMD_ACTIVATE);
        spi_byte(0x73);
        csn(1);
        write_register(NRF_REG_FEATURE, 0x04);
    }
    clear_irqs();
    irq_event = 0;
    command(NRF_CMD_FLUSH_TX);
    command(NRF_CMD_FLUSH_RX);
    write_register(NRF_REG_CONFIG, 0x0e);
    platform_delay_ms(2);

    return read_register(NRF_REG_RF_CH) == 0x02
        && read_register(NRF_REG_RF_SETUP) == 0x0e
        && read_register(NRF_REG_FEATURE) == 0x04;
}

int nrf24_irq_active(void)
{
    return GPIO_ReadInputDataBit(GPIOB, NRF_IRQ_PIN) == Bit_RESET;
}

void nrf24_recover(void)
{
    ce(0);
    clear_irqs();
    command(NRF_CMD_FLUSH_TX);
}

enum nrf24_result nrf24_send(const uint8_t payload[32], uint32_t timeout_ms)
{
    uint8_t status;
    const uint32_t start = platform_millis();

    nrf24_recover();
    irq_event = 0;
    csn(0);
    spi_byte(NRF_CMD_W_TX_PAYLOAD);
    for (unsigned index = 0; index < 32; ++index) {
        spi_byte(payload[index]);
    }
    csn(1);
    ce(1);
    platform_delay_us(15);
    ce(0);

    do {
        if (irq_event || nrf24_irq_active()) {
            irq_event = 0;
            status = command(NRF_CMD_NOP);
            if ((status & NRF_STATUS_TX_DS) != 0) {
                write_register(NRF_REG_STATUS, NRF_STATUS_TX_DS);
                return NRF24_TX_OK;
            }
            if ((status & NRF_STATUS_MAX_RT) != 0) {
                write_register(NRF_REG_STATUS, NRF_STATUS_MAX_RT);
                command(NRF_CMD_FLUSH_TX);
                return NRF24_TX_MAX_RT;
            }
            if ((status & NRF_STATUS_RX_DR) != 0) {
                write_register(NRF_REG_STATUS, NRF_STATUS_RX_DR);
            }
        }
    } while ((uint32_t)(platform_millis() - start) < timeout_ms);

    nrf24_recover();
    return NRF24_TX_TIMEOUT;
}
