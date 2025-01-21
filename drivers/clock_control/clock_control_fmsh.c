/**
 * @file clock_control_fmsh.c
 * @author Liao,Yuan Kai(savent_gate@outlook.com)
 * @brief Clock control driver for FMSH
 * @date 2024-10-10
 *
 * Copyright (c) 2024 SYSTech Co.
 *
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/fmsh_psoc_clock.h>
#include <zephyr/drivers/syscon.h>
#include <zephyr/sys/printk.h>

#define DT_DRV_COMPAT      fmsh_psoc_clkc
#define FMSH_INIT_PRIORITY CONFIG_SYSCON_INIT_PRIORITY + 1

#if CONFIG_CLOCK_CONTROL_FMSH_DEBUG
#define DBG_PRINTK printk
#define ASSERT_NO_PYBASS(reg)                                                                      \
	__ASSERT(GET_REG_BITS(reg, xxx_PLL_CTRL_BYPASS_FORCE) !=                                   \
				 xxx_PLL_CTRL_BYPASS_FORCE_ENABLE ||                               \
			 GET_REG_BITS(reg, xxx_PLL_CTRL_BYPASS_QUAL) !=                            \
				 xxx_PLL_CTRL_BYPASS_QUAL_ENABLE,                                  \
		 "PLL bypass not supported\n")
#else
#define DBG_PRINTK(...)
#define ASSERT_NO_PYBASS(reg)
#endif

#define SLCR     DT_NODELABEL(slcr)
#define SLCR_DEV DEVICE_DT_GET(SLCR)
#define CLKC     DT_NODELABEL(clkc)
#define CLKC_DEV DEVICE_DT_GET(CLKC)

/* Register offset */
#define REG_SCL                     (0x0000)
#define REG_SLCR_LOCK               (0x0004)
#define REG_SLCR_UNLOCK             (0x0008)
#define REG_SCLR_LOCKSTA            (0x000C)
#define REG_CPU_PLL_CTRL            (0x0100)
#define REG_CPU_PLL_CLKOUT0_DIVISOR (0x0104)
#define REG_CPU_PLL_CLKOUT1_DIVISOR (0x0108)
#define REG_CPU_PLL_CLKOUT2_DIVISOR (0x010C)
#define REG_CPU_PLL_CLKOUT3_DIVISOR (0x0120)
#define REG_CPU_PLL_CLKOUT4_DIVISOR (0x0124)
#define REG_CPU_PLL_CLKOUT5_DIVISOR (0x0128)
#define REG_DDR_PLL_CTRL            (0x0130)
#define REG_DDR_PLL_CLKOUT0_DIVISOR (0x0134)
#define REG_DDR_PLL_CLKOUT1_DIVISOR (0x0138)
#define REG_DDR_PLL_CLKOUT2_DIVISOR (0x013C)
#define REG_DDR_PLL_CLKOUT3_DIVISOR (0x0140)
#define REG_DDR_PLL_CLKOUT4_DIVISOR (0x0144)
#define REG_DDR_PLL_CLKOUT5_DIVISOR (0x0148)
#define REG_IO_PLL_CTRL             (0x0150)
#define REG_IO_PLL_CLKOUT0_DIVISOR  (0x0154)
#define REG_IO_PLL_CLKOUT1_DIVISOR  (0x0158)
#define REG_IO_PLL_CLKOUT2_DIVISOR  (0x015C)
#define REG_IO_PLL_CLKOUT3_DIVISOR  (0x0160)
#define REG_IO_PLL_CLKOUT4_DIVISOR  (0x0164)
#define REG_IO_PLL_CLKOUT5_DIVISOR  (0x0168)
#define REG_PLL_STATUS              (0x0170)
#define REG_CPU_PLL_CFG             (0x0174)
#define REG_DDR_PLL_CFG             (0x0178)
#define REG_IO_PLL_CFG              (0x017C)
#define REG_CPU_PLL_LATCH_EN        (0x0180)
#define REG_DDR_PLL_LATCH_EN        (0x0184)
#define REG_IO_PLL_LATCH_EN         (0x0188)
#define REG_PSS_RST_CTRL            (0x0200)
#define REG_CLK_621_TRUE            (0x020C)
#define REG_CPU_CLK_CTRL            (0x0210)
#define REG_CPU_RST_CTRL            (0x0214)
#define REG_GTC_CLK_CTRL            (0x022c)
#define REG_FPGA_CLK_CTRL           (0x0230)
#define REG_GEM0_CLK_CTRL           (0x0268)
#define REG_GEM1_CLK_CTRL           (0x026c)
#define REG_NFC_CLK_CTRL            (0x027c)
#define REG_QSPI_CLK_CTRL           (0x0284)
#define REG_SDIO_CLK_CTRL           (0x028c)
#define REG_UART_CLK_CTRL           (0x0294)
#define REG_SPI_CLK_CTRL            (0x029c)
#define REG_CAN_CLK_CTRL            (0x0304)
#define REG_GPIO_CLK_CTRL           (0x030c)
#define REG_WDT_RST_CTRL            (0x0330)
#define REG_TTC_CLK_CTRL            (0x0334)

/* Macros for register bits */
#define GET_REG_BITS(reg, bit_name) ((reg >> bit_name##_SHIFT) & bit_name##_MASK)
#define MASK(bit_name)              ((bit_name##_MASK << bit_name##_SHIFT))

/* Value for SLCR_UNLOCK and SLCR_LOCK */
#define SLCR_KEY 0xDF0D767B

/* Value for XXX_PLL_CTRL (xxx = CPU, DDR, IO) */
#define xxx_PLL_CTRL_FDIV_MASK           BIT_MASK(7)
#define xxx_PLL_CTRL_FDIV_SHIFT          16
#define xxx_PLL_CTRL_BYPASS_QUAL_MASK    BIT_MASK(1)
#define xxx_PLL_CTRL_BYPASS_QUAL_SHIFT   2
#define xxx_PLL_CTRL_BYPASS_QUAL_ENABLE  1
#define xxx_PLL_CTRL_BYPASS_FORCE_MASK   BIT_MASK(1)
#define xxx_PLL_CTRL_BYPASS_FORCE_SHIFT  3
#define xxx_PLL_CTRL_BYPASS_FORCE_ENABLE 1

/* Value for xxx_PLL_CLKOUTn_DIVISOR (xxx = CPU, DDR, IO, n=0..5) */
#define xxx_PLL_CLKOUTn_DIVISOR_DIVISOR_MASK  BIT_MASK(6)
#define xxx_PLL_CLKOUTn_DIVISOR_DIVISOR_SHIFT 0

/* Value for CPU_CLK_CTRL */
#define CPU_SRCSEL_MASK      BIT_MASK(2)
#define CPU_SRCSEL_SHIFT     0
#define CPU_SRCSEL_CPU_PLL   0
#define CPU_SRCSEL_DDR_PLL   2
#define CPU_SRCSEL_IO_PLL    3
#define CPU_AXI_CLKACT_MASK  BIT_MASK(1)
#define CPU_AXI_CLKACT_SHIFT 8
#define CPU_CLKACT_MASK      BIT_MASK(1)
#define CPU_CLKACT_SHIFT     9

/* Value for CLK_621_TRUE */
#define CLK_621_TRUE_DIV_MASK  BIT_MASK(1)
#define CLK_621_TRUE_DIV_SHIFT 0
#define CLK_621_TRUE_DIV3      1
#define CLK_621_TRUE_DIV2      0

/* Value for FPGA_CLK_CTRL */
#define FPGA0_CLKACT_MASK             BIT_MASK(1)
#define FPGA0_CLKACT_SHIFT            0
#define FPGA1_CLKACT_MASK             BIT_MASK(1)
#define FPGA1_CLKACT_SHIFT            1
#define FPGA2_CLKACT_MASK             BIT_MASK(1)
#define FPGA2_CLKACT_SHIFT            2
#define FPGA3_CLKACT_MASK             BIT_MASK(1)
#define FPGA3_CLKACT_SHIFT            3
#define FPGA_CLK_CTRL_DIV_MASK        BIT_MASK(6 * 4)
#define FPGA_CLK_CTRL_DIV_SHIFT       4
#define FPGA_CLK_CTRL_DIV_BIT_PER_CLK 6
#define FPGA_CLK_CTRL_ACT_MASK        BIT_MASK(4)
#define FPGA_CLK_CTRL_ACT_SHIFT       0

/* Value for NFC_CLK_CTRL */
#define NFC_REF_CLKACT_MASK   BIT_MASK(1)
#define NFC_REF_CLKACT_SHIFT  0
#define NFC_AHB_CLKACT_MASK   BIT_MASK(1)
#define NFC_AHB_CLKACT_SHIFT  1
#define NFC_REF_CLK_DIV_MASK  BIT_MASK(6)
#define NFC_REF_CLK_DIV_SHIFT 4

/* Value for UART_CLK_CTRL */
#define UART0_CLKACT_MASK      BIT_MASK(1)
#define UART0_CLKACT_SHIFT     0
#define UART1_CLKACT_MASK      BIT_MASK(1)
#define UART1_CLKACT_SHIFT     1
#define UART0_APB_CLKACT_MASK  BIT_MASK(1)
#define UART0_APB_CLKACT_SHIFT 2
#define UART1_APB_CLKACT_MASK  BIT_MASK(1)
#define UART1_APB_CLKACT_SHIFT 3
#define UART_SRCSEL_MASK       BIT_MASK(1)
#define UART_SRCSEL_SHIFT      4
#define UART_SRCSEL_CPU_PLL    1
#define UART_SRCSEL_IO_PLL     0

/* Value for QSPI_CLK_CTRL */
#define QSPI_CLKACT_MASK      BIT_MASK(1)
#define QSPI_CLKACT_SHIFT     0
#define QSPI_APB_CLKACT_MASK  BIT_MASK(1)
#define QSPI_APB_CLKACT_SHIFT 2
#define QSPI_AHB_CLKACT_MASK  BIT_MASK(1)
#define QSPI_AHB_CLKACT_SHIFT 3
#define QSPI_SRCSEL_MASK      BIT_MASK(1)
#define QSPI_SRCSEL_SHIFT     4
#define QSPI_SRCSEL_CPU_PLL   1
#define QSPI_SRCSEL_IO_PLL    0

/* Value for CAN_CLK_CTRL */
#define CAN0_APB_CLKACT_MASK  BIT_MASK(1)
#define CAN0_APB_CLKACT_SHIFT 0
#define CAN1_APB_CLKACT_MASK  BIT_MASK(1)
#define CAN1_APB_CLKACT_SHIFT 1

/* Value for GPIO_CLK_CTRL */
#define GPIO_APB_CLKACT_MASK  BIT_MASK(1)
#define GPIO_APB_CLKACT_SHIFT 0

/* Value for I2C_CLK_CTRL */
#define I2C0_APB_CLKACT_MASK  BIT_MASK(1)
#define I2C0_APB_CLKACT_SHIFT 0
#define I2C1_APB_CLKACT_MASK  BIT_MASK(1)
#define I2C1_APB_CLKACT_SHIFT 1

/* Value for SDIO_CLK_CTRL */
#define SDIO0_CLKACT_MASK      BIT_MASK(1)
#define SDIO0_CLKACT_SHIFT     0
#define SDIO1_CLKACT_MASK      BIT_MASK(1)
#define SDIO1_CLKACT_SHIFT     1
#define SDIO0_AHB_CLKACT_MASK  BIT_MASK(1)
#define SDIO0_AHB_CLKACT_SHIFT 2
#define SDIO1_AHB_CLKACT_MASK  BIT_MASK(1)
#define SDIO1_AHB_CLKACT_SHIFT 3
#define SDIO_SRCSEL_MASK       BIT_MASK(1)
#define SDIO_SRCSEL_SHIFT      4
#define SDIO_SRCSEL_CPU_PLL    1
#define SDIO_SRCSEL_IO_PLL     0

/* Value for GTC_CLK_CTRL */
#define GTC_DIVISOR_MASK  BIT_MASK(6)
#define GTC_DIVISOR_SHIFT 0

/* Value for SPI_CLK_CTRL */
#define SPI0_CLKACT_MASK      BIT_MASK(1)
#define SPI0_CLKACT_SHIFT     0
#define SPI1_CLKACT_MASK      BIT_MASK(1)
#define SPI1_CLKACT_SHIFT     1
#define SPI0_APB_CLKACT_MASK  BIT_MASK(1)
#define SPI0_APB_CLKACT_SHIFT 2
#define SPI1_APB_CLKACT_MASK  BIT_MASK(1)
#define SPI1_APB_CLKACT_SHIFT 3
#define SPI_SRCSEL_MASK       BIT_MASK(1)
#define SPI_SRCSEL_SHIFT      4
#define SPI_SRCSEL_CPU_PLL    1
#define SPI_SRCSEL_IO_PLL     0

/* Value for GEMx_CLK_CTRL */
#define GEMx_TX_SRCSEL_MASK       BIT_MASK(2)
#define GEMx_TX_SRCSEL_SHIFT      6
#define GEMx_TX_SRCSEL_IO_PLL     0
#define GEMx_TX_SRCSEL_CPU_PLL    1
#define GEMx_TX_SRCSEL_MIO_EMIO   2
#define GEMx_TX_SRCSEL_MIO_EMIO_1 3
#define GEMx_TX_DIV_MASK          BIT_MASK(6)
#define GEMx_TX_DIV_SHIFT         9
#define GEMx_TX_CLKACT_MASK       BIT_MASK(1)
#define GEMx_TX_CLKACT_SHIFT      0
#define GEMx_RX_CLKACT_MASK       BIT_MASK(1)
#define GEMx_RX_CLKACT_SHIFT      1
#define GEMx_AXI_CLKACT_MASK      BIT_MASK(1)
#define GEMx_AXI_CLKACT_SHIFT     3
#define GEMx_AHB_CLKACT_MASK      BIT_MASK(1)
#define GEMx_AHB_CLKACT_SHIFT     4

/* Value for TTC_CLK_CTRL */
#define TTC_REF_SEL(reg, instance, idx) ((reg >> ((instance * 3 + idx) * 2)) & 0x3)
#define TCC_REF_SEL_APB                 0
#define TCC_REF_SEL_PS_CLK              1
#define TCC_REF_SEL_MIO_EMIO            2
#define TCC_REF_SEL_MIO_EMIO_1          3
#define TTC0_APB_CLKACT_MASK            BIT_MASK(1)
#define TTC0_APB_CLKACT_SHIFT           12
#define TTC1_APB_CLKACT_MASK            BIT_MASK(1)
#define TTC1_APB_CLKACT_SHIFT           13
#define TTC0_CLK1ACT_MASK               BIT_MASK(1)
#define TTC0_CLK1ACT_SHIFT              14
#define TTC0_CLK2ACT_MASK               BIT_MASK(1)
#define TTC0_CLK2ACT_SHIFT              15
#define TTC0_CLK3ACT_MASK               BIT_MASK(1)
#define TTC0_CLK3ACT_SHIFT              16
#define TTC1_CLK1ACT_MASK               BIT_MASK(1)
#define TTC1_CLK1ACT_SHIFT              17
#define TTC1_CLK2ACT_MASK               BIT_MASK(1)
#define TTC1_CLK2ACT_SHIFT              18
#define TTC1_CLK3ACT_MASK               BIT_MASK(1)
#define TTC1_CLK3ACT_SHIFT              19

/* Value for WDT_CLK_CTRL */
#define WDT_SRCSEL_MASK       BIT_MASK(2)
#define WDT_SRCSEL_SHIFT      0
#define WDT_SRCSEL_APB        0
#define WDT_SRCSEL_PS_CLK     1
#define WDT_SRCSEL_MIO_EMIO   2
#define WDT_SRCSEL_MIO_EMIO_1 3
#define WDT_APB_CLKACT_MASK   BIT_MASK(1)
#define WDT_APB_CLKACT_SHIFT  2
#define WDT_CLKACT_MASK       BIT_MASK(1)
#define WDT_CLKACT_SHIFT      3

struct clock_control_fmsh_config {
	uint32_t ps_freq;
};

enum fmsh_clkid {
	fmsh_clkid_cpu = CLKID_CPU,
	fmsh_clkid_axi = CLKID_AXI,
	fmsh_clkid_ahb = CLKID_AHB,
	fmsh_clkid_apb = CLKID_APB,
	fmsh_clkid_ddrx1 = CLKID_DDRX1,
	fmsh_clkid_ddrx4 = CLKID_DDRX4,
	fmsh_clkid_uart0 = CLKID_UART0,
	fmsh_clkid_uart1 = CLKID_UART1,
	fmsh_clkid_gtc = CLKID_GTC,
	fmsh_clkid_fclk0 = CLKID_FCLK0,
	fmsh_clkid_fclk1 = CLKID_FCLK1,
	fmsh_clkid_fclk2 = CLKID_FCLK2,
	fmsh_clkid_fclk3 = CLKID_FCLK3,
	fmsh_clkid_nfc = CLKID_NFC,
	fmsh_clkid_sdmmc0 = CLKID_SDMMC0,
	fmsh_clkid_sdmmc1 = CLKID_SDMMC1,
	fmsh_clkid_spi0 = CLKID_SPI0,
	fmsh_clkid_spi1 = CLKID_SPI1,
	fmsh_clkid_qspi = CLKID_QSPI,
	fmsh_clkid_can0 = CLKID_CAN0,
	fmsh_clkid_can1 = CLKID_CAN1,
	fmsh_clkid_i2c0 = CLKID_I2C0,
	fmsh_clkid_i2c1 = CLKID_I2C1,
	fmsh_clkid_ttc0_ref1 = CLKID_TTC0_REF1,
	fmsh_clkid_ttc0_ref2 = CLKID_TTC0_REF2,
	fmsh_clkid_ttc0_ref3 = CLKID_TTC0_REF3,
	fmsh_clkid_ttc1_ref1 = CLKID_TTC1_REF1,
	fmsh_clkid_ttc1_ref2 = CLKID_TTC1_REF2,
	fmsh_clkid_ttc1_ref3 = CLKID_TTC1_REF3,
	fmsh_clkid_ttc0_apb = CLKID_TTC0_APB,
	fmsh_clkid_ttc1_apb = CLKID_TTC1_APB,
	fmsh_clkid_wdt = CLKID_WDT,
	fmsh_clkid_gpio = CLKID_GPIO,
	fmsh_clkid_gem0 = CLKID_GEM0,
	fmsh_clkid_gem1 = CLKID_GEM1,
	fmsh_clkid_max,
};

static const uint32_t fmsh_clkid_ctrl_reg[fmsh_clkid_max] = {
	[CLKID_CPU] = REG_CPU_CLK_CTRL,       [CLKID_UART0] = REG_UART_CLK_CTRL,
	[CLKID_UART1] = REG_UART_CLK_CTRL,    [CLKID_GTC] = REG_GTC_CLK_CTRL,
	[CLKID_FCLK0] = REG_FPGA_CLK_CTRL,    [CLKID_FCLK1] = REG_FPGA_CLK_CTRL,
	[CLKID_FCLK2] = REG_FPGA_CLK_CTRL,    [CLKID_FCLK3] = REG_FPGA_CLK_CTRL,
	[CLKID_NFC] = REG_NFC_CLK_CTRL,       [CLKID_SDMMC0] = REG_SDIO_CLK_CTRL,
	[CLKID_SDMMC1] = REG_SDIO_CLK_CTRL,   [CLKID_SPI0] = REG_SPI_CLK_CTRL,
	[CLKID_SPI1] = REG_SPI_CLK_CTRL,      [CLKID_QSPI] = REG_QSPI_CLK_CTRL,
	[CLKID_CAN0] = REG_CAN_CLK_CTRL,      [CLKID_CAN1] = REG_CAN_CLK_CTRL,
	[CLKID_I2C0] = REG_UART_CLK_CTRL,     [CLKID_I2C1] = REG_UART_CLK_CTRL,
	[CLKID_TTC0_REF1] = REG_TTC_CLK_CTRL, [CLKID_TTC0_REF2] = REG_TTC_CLK_CTRL,
	[CLKID_TTC0_REF3] = REG_TTC_CLK_CTRL, [CLKID_TTC1_REF1] = REG_TTC_CLK_CTRL,
	[CLKID_TTC1_REF2] = REG_TTC_CLK_CTRL, [CLKID_TTC1_REF3] = REG_TTC_CLK_CTRL,
	[CLKID_TTC0_APB] = REG_TTC_CLK_CTRL,  [CLKID_TTC1_APB] = REG_TTC_CLK_CTRL,
	[CLKID_WDT] = REG_WDT_RST_CTRL,       [CLKID_GPIO] = REG_GPIO_CLK_CTRL,
	[CLKID_GEM0] = REG_GEM0_CLK_CTRL,     [CLKID_GEM1] = REG_GEM1_CLK_CTRL,
};

static const uint32_t fmsh_clkid_ctrl_gate[fmsh_clkid_max] = {
	[CLKID_CPU] = MASK(CPU_CLKACT) | MASK(CPU_AXI_CLKACT),
	[CLKID_UART0] = MASK(UART0_CLKACT) | MASK(UART0_APB_CLKACT),
	[CLKID_UART1] = MASK(UART1_CLKACT) | MASK(UART1_APB_CLKACT),
	[CLKID_GTC] = 0, /* No clock gate */
	[CLKID_FCLK0] = MASK(FPGA0_CLKACT),
	[CLKID_FCLK1] = MASK(FPGA1_CLKACT),
	[CLKID_FCLK2] = MASK(FPGA2_CLKACT),
	[CLKID_FCLK3] = MASK(FPGA3_CLKACT),
	[CLKID_NFC] = MASK(NFC_REF_CLKACT) | MASK(NFC_AHB_CLKACT),
	[CLKID_SDMMC0] = MASK(SDIO0_CLKACT) | MASK(SDIO0_AHB_CLKACT),
	[CLKID_SDMMC1] = MASK(SDIO1_CLKACT) | MASK(SDIO1_AHB_CLKACT),
	[CLKID_SPI0] = MASK(SPI0_CLKACT) | MASK(SPI0_APB_CLKACT),
	[CLKID_SPI1] = MASK(SPI1_CLKACT) | MASK(SPI1_APB_CLKACT),
	[CLKID_QSPI] = MASK(QSPI_CLKACT) | MASK(QSPI_APB_CLKACT) | MASK(QSPI_AHB_CLKACT),
	[CLKID_CAN0] = MASK(CAN0_APB_CLKACT),
	[CLKID_CAN1] = MASK(CAN1_APB_CLKACT),
	[CLKID_I2C0] = MASK(I2C0_APB_CLKACT),
	[CLKID_I2C1] = MASK(I2C1_APB_CLKACT),
	[CLKID_TTC0_REF1] = MASK(TTC0_CLK1ACT),
	[CLKID_TTC0_REF2] = MASK(TTC0_CLK2ACT),
	[CLKID_TTC0_REF3] = MASK(TTC0_CLK3ACT),
	[CLKID_TTC1_REF1] = MASK(TTC1_CLK1ACT),
	[CLKID_TTC1_REF2] = MASK(TTC1_CLK2ACT),
	[CLKID_TTC1_REF3] = MASK(TTC1_CLK3ACT),
	[CLKID_TTC0_APB] = MASK(TTC0_APB_CLKACT),
	[CLKID_TTC1_APB] = MASK(TTC1_APB_CLKACT),
	[CLKID_WDT] = MASK(WDT_CLKACT) | MASK(WDT_APB_CLKACT),
	[CLKID_GPIO] = MASK(GPIO_APB_CLKACT),
	[CLKID_GEM0] = MASK(GEMx_TX_CLKACT) | MASK(GEMx_RX_CLKACT) | MASK(GEMx_AXI_CLKACT) |
		       MASK(GEMx_AHB_CLKACT),
	[CLKID_GEM1] = MASK(GEMx_TX_CLKACT) | MASK(GEMx_RX_CLKACT) | MASK(GEMx_AXI_CLKACT) |
		       MASK(GEMx_AHB_CLKACT),
};

static inline void fmsh_clkc_unlock(void)
{
	syscon_write_reg(SLCR_DEV, REG_SLCR_UNLOCK, SLCR_KEY);
}
static inline void fmsh_clkc_lock(void)
{
	syscon_write_reg(SLCR_DEV, REG_SLCR_LOCK, SLCR_KEY);
}

static inline uint32_t fmsh_clkc_get_cpu_freq(uint32_t ps_clk, uint32_t out_clk)
{
	uint32_t pll_ctl, cpu_clk_ctl, clk_div;
	uint32_t pll_m, pll_o, cpu_freq;

	DBG_PRINTK("Get CPU out clk %d freq\n", out_clk);
	__ASSERT(out_clk < 6, "Invalid IO out clk %d\n", out_clk);
	out_clk *= 4;
	syscon_read_reg(SLCR_DEV, REG_CPU_CLK_CTRL, &cpu_clk_ctl);
	switch (GET_REG_BITS(cpu_clk_ctl, CPU_SRCSEL)) {
	case CPU_SRCSEL_CPU_PLL:
		DBG_PRINTK("\tCPU source: CPU PLL\n");
		syscon_read_reg(SLCR_DEV, REG_CPU_PLL_CTRL, &pll_ctl);
		syscon_read_reg(SLCR_DEV, REG_CPU_PLL_CLKOUT0_DIVISOR + out_clk, &clk_div);
		break;
	case CPU_SRCSEL_DDR_PLL:
		DBG_PRINTK("\tCPU source: DDR PLL\n");
		syscon_read_reg(SLCR_DEV, REG_DDR_PLL_CTRL, &pll_ctl);
		syscon_read_reg(SLCR_DEV, REG_DDR_PLL_CLKOUT0_DIVISOR + out_clk, &clk_div);
		break;
	case CPU_SRCSEL_IO_PLL:
		DBG_PRINTK("\tCPU source: IO PLL\n");
		syscon_read_reg(SLCR_DEV, REG_IO_PLL_CTRL, &pll_ctl);
		syscon_read_reg(SLCR_DEV, REG_IO_PLL_CLKOUT0_DIVISOR + out_clk, &clk_div);
		break;
	default:
		DBG_PRINTK("\tCPU source: Unknown:%ld\n", GET_REG_BITS(cpu_clk_ctl, CPU_SRCSEL));
		__ASSERT(0, "Unknown CPU source\n");
		return -1;
	}
	ASSERT_NO_PYBASS(pll_ctl);
	pll_m = GET_REG_BITS(pll_ctl, xxx_PLL_CTRL_FDIV);
	pll_o = GET_REG_BITS(clk_div, xxx_PLL_CLKOUTn_DIVISOR_DIVISOR);
	cpu_freq = ps_clk * pll_m / pll_o;
	DBG_PRINTK("\tfreq(%d) = ps_clk(%d) * M(%d) / O(%d)\n", cpu_freq, ps_clk, pll_m, pll_o);
	return cpu_freq;
}

static inline uint32_t fmsh_clkc_get_axi_freq(uint32_t ps_clk)
{
	uint32_t div;
	uint32_t cpu_freq;

	cpu_freq = fmsh_clkc_get_cpu_freq(ps_clk, 0);
	syscon_read_reg(SLCR_DEV, REG_CLK_621_TRUE, &div);

	if (GET_REG_BITS(div, CLK_621_TRUE_DIV) == CLK_621_TRUE_DIV3) {
		div = 3;
	} else {
		div = 2;
	}
	DBG_PRINTK("Get AXI freq\n");
	DBG_PRINTK("\tAXI freq(%d) = CPU freq(%d) / div(%d)\n", cpu_freq / div, cpu_freq, div);
	return cpu_freq / div;
}

static inline uint32_t fmsh_clkc_get_ahb_freq(uint32_t ps_clk)
{
	DBG_PRINTK("Get AHB freq\n");
	DBG_PRINTK("\tAHB freq(%d) = AXI freq(%d) / 2\n", fmsh_clkc_get_axi_freq(ps_clk) / 2,
		   fmsh_clkc_get_axi_freq(ps_clk));
	return fmsh_clkc_get_axi_freq(ps_clk) / 2;
}

static inline uint32_t fmsh_clkc_get_apb_freq(uint32_t ps_clk)
{
	DBG_PRINTK("Get APB freq\n");
	DBG_PRINTK("\tAPB freq(%d) = AHB freq(%d) / 2\n", fmsh_clkc_get_ahb_freq(ps_clk) / 2,
		   fmsh_clkc_get_ahb_freq(ps_clk));
	return fmsh_clkc_get_ahb_freq(ps_clk) / 2;
}

static inline uint32_t fmsh_clkc_get_gtc_freq(uint32_t ps_clk)
{
	uint32_t div;

	DBG_PRINTK("Get GTC freq\n");
	syscon_read_reg(SLCR_DEV, REG_GTC_CLK_CTRL, &div);
	div = GET_REG_BITS(div, GTC_DIVISOR);
	DBG_PRINTK("\tGTC freq(%d) = CPU freq(%d) / div(%d)\n", ps_clk / div, ps_clk, div);
	return ps_clk / div;
}

static inline uint32_t fmsh_clkc_get_ddr_freq(uint32_t ps_clk, uint32_t out_clk)
{
	uint32_t pll_ctl, clk_div;
	uint32_t pll_m, pll_o, ddr_freq;

	DBG_PRINTK("Get DDR out clk %d freq\n", out_clk);
	__ASSERT(out_clk < 6, "Invalid IO out clk %d\n", out_clk);

	out_clk *= 4;
	syscon_read_reg(SLCR_DEV, REG_DDR_PLL_CTRL, &pll_ctl);
	syscon_read_reg(SLCR_DEV, REG_DDR_PLL_CLKOUT0_DIVISOR + out_clk, &clk_div);

	ASSERT_NO_PYBASS(pll_ctl);
	pll_m = GET_REG_BITS(pll_ctl, xxx_PLL_CTRL_FDIV);
	pll_o = GET_REG_BITS(clk_div, xxx_PLL_CLKOUTn_DIVISOR_DIVISOR);
	ddr_freq = ps_clk * pll_m / pll_o;

	DBG_PRINTK("\tfreq(%d) = ps_clk(%d) * M(%d) / O(%d)\n", ddr_freq, ps_clk, pll_m, pll_o);
	return ddr_freq;
}

static inline uint32_t fmsh_clkc_get_fclk_freq(uint32_t ps_clk, uint32_t idx)
{
	__ASSERT(idx < 4, "Invalid FCLK index %d\n", idx);
	uint32_t div;
	uint32_t ddr_freq;

	ddr_freq = fmsh_clkc_get_ddr_freq(ps_clk, idx + 2); /* DDR clkout2~5 are used for FCLK0~3 */
	syscon_read_reg(SLCR_DEV, REG_FPGA_CLK_CTRL, &div);
	div = (GET_REG_BITS(div, FPGA_CLK_CTRL_DIV) >> (FPGA_CLK_CTRL_DIV_BIT_PER_CLK * idx)) &
	      BIT_MASK(FPGA_CLK_CTRL_DIV_BIT_PER_CLK);
	DBG_PRINTK("Get FCLK%d freq\n", idx);
	DBG_PRINTK("\tFCLK%d freq(%d) = DDR freq(%d) / div(%d)\n", idx, ddr_freq / div, ddr_freq,
		   div);
	return ddr_freq / div;
}

static inline uint32_t fmsh_clkc_get_io_freq(uint32_t pc_clk, uint32_t out_clk)
{
	uint32_t pll_ctl, clk_div;
	uint32_t pll_m, pll_o, io_freq;

	DBG_PRINTK("Get IO out clk %d freq\n", out_clk);
	__ASSERT(out_clk < 6, "Invalid IO out clk %d\n", out_clk);

	out_clk *= 4;
	syscon_read_reg(SLCR_DEV, REG_IO_PLL_CTRL, &pll_ctl);
	syscon_read_reg(SLCR_DEV, REG_IO_PLL_CLKOUT0_DIVISOR + out_clk, &clk_div);

	ASSERT_NO_PYBASS(pll_ctl);
	pll_m = GET_REG_BITS(pll_ctl, xxx_PLL_CTRL_FDIV);
	pll_o = GET_REG_BITS(clk_div, xxx_PLL_CLKOUTn_DIVISOR_DIVISOR);
	io_freq = pc_clk * pll_m / pll_o;

	DBG_PRINTK("\tfreq(%d) = pc_clk(%d) * M(%d) / O(%d)\n", io_freq, pc_clk, pll_m, pll_o);
	return io_freq;
}

static inline uint32_t fmsh_clkc_get_nfc_freq(uint32_t ps_clk)
{
	uint32_t div;
	uint32_t ahb_freq;

	ahb_freq = fmsh_clkc_get_ahb_freq(ps_clk);
	syscon_read_reg(SLCR_DEV, REG_NFC_CLK_CTRL, &div);
	div = GET_REG_BITS(div, NFC_REF_CLK_DIV);
	DBG_PRINTK("Get NFC freq\n");
	DBG_PRINTK("\tNFC freq(%d) = AHB freq(%d) / div(%d)\n", ahb_freq / div, ahb_freq, div);
	return ahb_freq / div;
}

static inline uint32_t fmsh_clkc_get_qspi_freq(uint32_t ps_clk)
{
	uint32_t clk_ctl, clk_freq;

	syscon_read_reg(SLCR_DEV, REG_QSPI_CLK_CTRL, &clk_ctl);
	if (GET_REG_BITS(clk_ctl, QSPI_SRCSEL) == QSPI_SRCSEL_CPU_PLL) {
		DBG_PRINTK("QSPI source: CPU PLL\n");
		clk_freq = fmsh_clkc_get_cpu_freq(ps_clk, 4);
	} else {
		DBG_PRINTK("QSPI source: IO PLL\n");
		clk_freq = fmsh_clkc_get_io_freq(ps_clk, 4);
	}
	DBG_PRINTK("Get QSPI freq\n");
	DBG_PRINTK("\tQSPI freq(%d)\n", clk_freq);
	return clk_freq;
}

static inline uint32_t fmsh_clkc_get_sdmmc_freq(uint32_t ps_clk)
{
	uint32_t clk_ctl, ref_freq;

	syscon_read_reg(SLCR_DEV, REG_SDIO_CLK_CTRL, &clk_ctl);
	if (GET_REG_BITS(clk_ctl, SDIO_SRCSEL) == SDIO_SRCSEL_CPU_PLL) {
		DBG_PRINTK("SDMMC source: CPU PLL\n");
		ref_freq = fmsh_clkc_get_cpu_freq(ps_clk, 2);
	} else {
		DBG_PRINTK("SDMMC source: IO PLL\n");
		ref_freq = fmsh_clkc_get_io_freq(ps_clk, 2);
	}
	DBG_PRINTK("Get SDMMC freq\n");
	DBG_PRINTK("\tSDMMC freq(%d) = ref freq(%d)\n", ref_freq, ref_freq);
	return ref_freq;
}

static inline uint32_t fmsh_clkc_get_uart_freq(uint32_t ps_clk)
{
	uint32_t clk_ctl, clk_freq;

	syscon_read_reg(SLCR_DEV, REG_UART_CLK_CTRL, &clk_ctl);
	if (GET_REG_BITS(clk_ctl, UART_SRCSEL) == UART_SRCSEL_CPU_PLL) {
		DBG_PRINTK("UART source: CPU PLL\n");
		clk_freq = fmsh_clkc_get_cpu_freq(ps_clk, 5);
	} else {
		DBG_PRINTK("UART source: IO PLL\n");
		clk_freq = fmsh_clkc_get_io_freq(ps_clk, 5);
	}
	DBG_PRINTK("\tUART freq(%d)\n", clk_freq);
	return clk_freq;
}

static inline uint32_t fmsh_clkc_get_spi_freq(uint32_t ps_clk)
{
	uint32_t clk_ctl, clk_freq;

	syscon_read_reg(SLCR_DEV, REG_SPI_CLK_CTRL, &clk_ctl);
	if (GET_REG_BITS(clk_ctl, SPI_SRCSEL) == SPI_SRCSEL_CPU_PLL) {
		DBG_PRINTK("SPI source: CPU PLL\n");
		clk_freq = fmsh_clkc_get_cpu_freq(ps_clk, 3);
	} else {
		DBG_PRINTK("SPI source: IO PLL\n");
		clk_freq = fmsh_clkc_get_io_freq(ps_clk, 3);
	}
	DBG_PRINTK("\tSPI freq(%d)\n", clk_freq);
	return clk_freq;
}

static inline uint32_t fmsh_clkc_get_ttcref_freq(uint32_t pc_clk, uint32_t instance, uint32_t idx)
{
	uint32_t clk_ctrl, ref_freq = -1, sel;

	DBG_PRINTK("Get TTC%d-%d ref freq\n", instance, idx);
	__ASSERT(instance < 2, "Invalid instance %d\n", instance);
	__ASSERT(idx < 3, "Invalid index %d\n", idx);

	syscon_read_reg(SLCR_DEV, REG_TTC_CLK_CTRL, &clk_ctrl);
	sel = TTC_REF_SEL(clk_ctrl, instance, idx);
	switch (sel) {
	case TCC_REF_SEL_APB:
		DBG_PRINTK("TTC%d-%d source: APB\n", instance, idx);
		ref_freq = fmsh_clkc_get_apb_freq(pc_clk);
		break;
	case TCC_REF_SEL_PS_CLK:
		DBG_PRINTK("TTC%d-%d source: PS_CLK\n", instance, idx);
		ref_freq = pc_clk;
		break;
	case TCC_REF_SEL_MIO_EMIO:
	case TCC_REF_SEL_MIO_EMIO_1:
		DBG_PRINTK("TTC%d-%d source: MIO/EMIO\n", instance, idx);
		DBG_PRINTK("Can't find the frequency of MIO/EMIO\n");
		ref_freq = -1;
		break;
	}
	return ref_freq;
}

static inline uint32_t fmsh_clkc_get_wdt_freq(uint32_t ps_clk)
{
	uint32_t clk_ctl, clk_freq = -1;

	syscon_read_reg(SLCR_DEV, REG_WDT_RST_CTRL, &clk_ctl);
	switch (GET_REG_BITS(clk_ctl, WDT_SRCSEL)) {
	case WDT_SRCSEL_APB:
		DBG_PRINTK("WDT source: APB\n");
		clk_freq = fmsh_clkc_get_apb_freq(ps_clk);
		break;
	case WDT_SRCSEL_PS_CLK:
		DBG_PRINTK("WDT source: PS_CLK\n");
		clk_freq = ps_clk;
		break;
	case WDT_SRCSEL_MIO_EMIO:
	case WDT_SRCSEL_MIO_EMIO_1:
		DBG_PRINTK("WDT source: MIO/EMIO\n");
		DBG_PRINTK("Can't find the frequency of MIO/EMIO\n");
		break;
	default:
		DBG_PRINTK("WDT source: Unknown\n");
		break;
	}
	DBG_PRINTK("\tWDT freq(%d)\n", clk_freq);
	return clk_freq;
}

static inline uint32_t fmsh_clkc_get_gem_freq(uint32_t ps_clk, uint32_t instance)
{
	uint32_t clk_ctl, clk_freq = -1;
	uint32_t reg_base;

	__ASSERT(instance < 2, "Invalid instance %d\n", instance);
	reg_base = REG_GEM0_CLK_CTRL + instance * 4;
	syscon_read_reg(SLCR_DEV, reg_base, &clk_ctl);
	switch (GET_REG_BITS(clk_ctl, GEMx_TX_SRCSEL)) {
	case GEMx_TX_SRCSEL_IO_PLL:
		DBG_PRINTK("GEM%d source: IO PLL\n", instance);
		clk_freq = fmsh_clkc_get_io_freq(ps_clk, 1);
		break;
	case GEMx_TX_SRCSEL_CPU_PLL:
		DBG_PRINTK("GEM%d source: CPU PLL\n", instance);
		clk_freq = fmsh_clkc_get_cpu_freq(ps_clk, 1);
		break;
	default:
		DBG_PRINTK("GEM%d source: MIO/EMIO\n", instance);
		DBG_PRINTK("Can't find the frequency of MIO/EMIO\n");
		return -1;
	}

	return clk_freq / GET_REG_BITS(clk_ctl, GEMx_TX_DIV);
}

static int fmsh_clkc_switch(uint32_t clkid, bool enable)
{
	uint32_t clk_ctl_reg, reg, gate;

	__ASSERT(clkid < ARRAY_SIZE(fmsh_clkid_ctrl_reg), "Invalid clkid %d\n", clkid);
	__ASSERT(clkid < ARRAY_SIZE(fmsh_clkid_ctrl_gate), "Invalid clkid %d\n", clkid);

	clk_ctl_reg = fmsh_clkid_ctrl_reg[clkid];
	gate = fmsh_clkid_ctrl_gate[clkid];

	if (clk_ctl_reg == 0 || gate == 0) {
		DBG_PRINTK("Unsupported clock id %d\n", clkid);
		return -EINVAL;
	}

	fmsh_clkc_unlock();
	if (enable) {
		syscon_read_reg(SLCR_DEV, clk_ctl_reg, &reg);
		reg |= gate;
		syscon_write_reg(SLCR_DEV, clk_ctl_reg, reg);
	} else {
		syscon_read_reg(SLCR_DEV, clk_ctl_reg, &reg);
		reg &= ~gate;
		syscon_write_reg(SLCR_DEV, clk_ctl_reg, reg);
	}
	fmsh_clkc_lock();

	return 0;
}

static int clock_control_fmsh_on(const struct device *dev, clock_control_subsys_t sys)
{
	return fmsh_clkc_switch((enum fmsh_clkid)sys, true);
}

static int clock_control_fmsh_off(const struct device *dev, clock_control_subsys_t sys)
{
	return fmsh_clkc_switch((enum fmsh_clkid)sys, false);
}

static int clock_control_fmsh_get_rate(const struct device *dev, clock_control_subsys_t sys,
				       uint32_t *rate)
{
	enum fmsh_clkid clkid = (enum fmsh_clkid)sys;
	const struct clock_control_fmsh_config *config = dev->config;

	switch (clkid) {
	case fmsh_clkid_cpu:
		*rate = fmsh_clkc_get_cpu_freq(config->ps_freq, 0);
		break;
	case fmsh_clkid_axi:
		*rate = fmsh_clkc_get_axi_freq(config->ps_freq);
		break;
	case fmsh_clkid_ahb:
		*rate = fmsh_clkc_get_ahb_freq(config->ps_freq);
		break;
	case fmsh_clkid_apb:
	case fmsh_clkid_ttc0_apb:
	case fmsh_clkid_ttc1_apb:
	case fmsh_clkid_gpio:
	case fmsh_clkid_can0:
	case fmsh_clkid_can1:
	case fmsh_clkid_i2c0:
	case fmsh_clkid_i2c1:
		*rate = fmsh_clkc_get_apb_freq(config->ps_freq);
		break;
	case fmsh_clkid_ddrx1:
		*rate = fmsh_clkc_get_ddr_freq(config->ps_freq, 0) / 4;
		break;
	case fmsh_clkid_ddrx4:
		*rate = fmsh_clkc_get_ddr_freq(config->ps_freq, 0);
		break;
	case fmsh_clkid_uart0:
	case fmsh_clkid_uart1:
		*rate = fmsh_clkc_get_uart_freq(config->ps_freq);
		break;
	case fmsh_clkid_gtc:
		*rate = fmsh_clkc_get_gtc_freq(config->ps_freq);
		break;
	case fmsh_clkid_fclk0:
		*rate = fmsh_clkc_get_fclk_freq(config->ps_freq, 0);
		break;
	case fmsh_clkid_fclk1:
		*rate = fmsh_clkc_get_fclk_freq(config->ps_freq, 1);
		break;
	case fmsh_clkid_fclk2:
		*rate = fmsh_clkc_get_fclk_freq(config->ps_freq, 2);
		break;
	case fmsh_clkid_fclk3:
		*rate = fmsh_clkc_get_fclk_freq(config->ps_freq, 3);
		break;
	case fmsh_clkid_nfc:
		*rate = fmsh_clkc_get_nfc_freq(config->ps_freq);
		break;
	case fmsh_clkid_sdmmc0:
	case fmsh_clkid_sdmmc1:
		*rate = fmsh_clkc_get_sdmmc_freq(config->ps_freq);
		break;
	case fmsh_clkid_spi0:
	case fmsh_clkid_spi1:
		*rate = fmsh_clkc_get_spi_freq(config->ps_freq);
		break;
	case fmsh_clkid_qspi:
		*rate = fmsh_clkc_get_qspi_freq(config->ps_freq);
		break;
	case fmsh_clkid_ttc0_ref1:
	case fmsh_clkid_ttc0_ref2:
	case fmsh_clkid_ttc0_ref3:
		*rate = fmsh_clkc_get_ttcref_freq(config->ps_freq, 0, clkid - fmsh_clkid_ttc0_ref1);
		break;
	case fmsh_clkid_ttc1_ref1:
	case fmsh_clkid_ttc1_ref2:
	case fmsh_clkid_ttc1_ref3:
		*rate = fmsh_clkc_get_ttcref_freq(config->ps_freq, 1, clkid - fmsh_clkid_ttc1_ref1);
		break;
	case fmsh_clkid_wdt:
		*rate = fmsh_clkc_get_wdt_freq(config->ps_freq);
		break;
	case fmsh_clkid_gem0:
		*rate = fmsh_clkc_get_gem_freq(config->ps_freq, 0);
		break;
	case fmsh_clkid_gem1:
		*rate = fmsh_clkc_get_gem_freq(config->ps_freq, 1);
		break;
	default:
		printk("Unsupported clock id %d\n", clkid);
		return -EINVAL;
	}
	return 0;
}

static const struct clock_control_driver_api clock_control_fmsh_api = {
	.on = clock_control_fmsh_on,
	.off = clock_control_fmsh_off,
	.get_rate = clock_control_fmsh_get_rate,
};

static const struct clock_control_fmsh_config config = {
	.ps_freq = DT_PROP(CLKC, ps_clk_frequency),
};

static int fmsh_clkc_init(const struct device *dev)
{

	ARG_UNUSED(dev);

#ifdef CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME
	/* Read GTC frequency */
	extern int z_clock_hw_cycles_per_sec;

	clock_control_get_rate(dev, (clock_control_subsys_t)CLKID_GTC, &z_clock_hw_cycles_per_sec);
#endif

	return 0;
}

/* FIXME: SYSCON should be initialized before clock_control */
#ifndef CONFIG_SYSCON
#define INIT_PRIORITY CONFIG_CLOCK_CONTROL_INIT_PRIORITY
#else
#define INIT_PRIORITY CONFIG_SYSCON_INIT_PRIORITY
#endif

DEVICE_DT_DEFINE(CLKC, fmsh_clkc_init, NULL, NULL, &config, PRE_KERNEL_1, INIT_PRIORITY,
		 &clock_control_fmsh_api);
