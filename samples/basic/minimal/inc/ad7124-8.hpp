#pragma once

#include <cstdint>
#include <cstddef>

#ifndef BIT
#define BIT(n) (1U << (n))
#endif

struct spi_iface {
	virtual ~spi_iface() = default;
	virtual bool xfer(uint8_t *tx, uint8_t *rx, uint8_t tx_size, uint8_t rx_size) = 0;
};

namespace adc7124_8
{
enum class reg : uint8_t {
	REG_STATUS = 0x00,
	REG_ADC_CTRL = 0x01,
	REG_DATA = 0x02,
	REG_IO_CTRL_1 = 0x03,
	REG_IO_CTRL_2 = 0x04,
	REG_ID = 0x05,
	REG_ERR = 0x06,
	REG_ERR_EN = 0x07,
	REG_MCLK_CNT = 0x08,
	REG_CHA_0 = 0x09,
	REG_CHA_1 = 0x0A,
	REG_CHA_2 = 0x0B,
	REG_CHA_3 = 0x0C,
	REG_CHA_4 = 0x0D,
	REG_CHA_5 = 0x0E,
	REG_CHA_6 = 0x0F,
	REG_CHA_7 = 0x10,
	REG_CHA_8 = 0x11,
	REG_CHA_9 = 0x12,
	REG_CHA_10 = 0x13,
	REG_CHA_11 = 0x14,
	REG_CHA_12 = 0x15,
	REG_CHA_13 = 0x16,
	REG_CHA_14 = 0x17,
	REG_CHA_15 = 0x18,
	REG_CONFIG_0 = 0x19,
	REG_CONFIG_1 = 0x1A,
	REG_CONFIG_2 = 0x1B,
	REG_CONFIG_3 = 0x1C,
	REG_CONFIG_4 = 0x1D,
	REG_CONFIG_5 = 0x1E,
	REG_CONFIG_6 = 0x1F,
	REG_CONFIG_7 = 0x20,
	REG_FILTER_0 = 0x21,
	REG_FILTER_1 = 0x22,
	REG_FILTER_2 = 0x23,
	REG_FILTER_3 = 0x24,
	REG_FILTER_4 = 0x25,
	REG_FILTER_5 = 0x26,
	REG_FILTER_6 = 0x27,
	REG_FILTER_7 = 0x28,
	REG_OFFSET_0 = 0x29,
	REG_OFFSET_1 = 0x2A,
	REG_OFFSET_2 = 0x2B,
	REG_OFFSET_3 = 0x2C,
	REG_OFFSET_4 = 0x2D,
	REG_OFFSET_5 = 0x2E,
	REG_OFFSET_6 = 0x2F,
	REG_OFFSET_7 = 0x30,
	REG_GAIN_0 = 0x31,
	REG_GAIN_1 = 0x32,
	REG_GAIN_2 = 0x33,
	REG_GAIN_3 = 0x34,
	REG_GAIN_4 = 0x35,
	REG_GAIN_5 = 0x36,
	REG_GAIN_6 = 0x37,
	REG_GAIN_7 = 0x38,
};

enum class status : uint8_t {
	STATUS_RDY_N = BIT(7),
	STATUS_ERR = BIT(6),
	STATUS_POR = BIT(4),
};

enum class adc_clock_ref : uint8_t {
	ADC_CLK_REF_INT = 0,
	ADC_CLK_REF_INT_OUT = 1, /* output internal clock at CLK pin */
	ADC_CLK_EXT = 2,
	ADC_CLK_EXT_DIV4 = 3,
};

enum class adc_pin_mux: uint8_t {
    ADC_PIN_MUX_DIFF_0 = 0, /* asign channel to use AI+0 and AI-1 */
    ADC_PIN_MUX_DIFF_1 = 1, /* asign channel to use AI+2 and AI-3 */
    ADC_PIN_MUX_DIFF_2 = 2, /* asign channel to use AI+4 and AI-5 */
    ADC_PIN_MUX_DIFF_3 = 3, /* asign channel to use AI+6 and AI-7 */
    ADC_PIN_MUX_DIFF_4 = 4, /* asign channel to use AI+8 and AI-9 */
    ADC_PIN_MUX_DIFF_5 = 5, /* asign channel to use AI+10 and AI-11 */
    ADC_PIN_MUX_DIFF_6 = 6, /* asign channel to use AI+12 and AI-13 */
    ADC_PIN_MUX_DIFF_7 = 7, /* asign channel to use AI+14 and AI-15 */
    ADC_PIN_MUX_DIFF_AUTO = 8, /* auto asign channel, AI+{ch*2} and AI-{ch*2+1} */
};

enum class adc_mode : uint8_t {
	ADC_MODE_CONTINUE = 0,
	ADC_MODE_ONESHOT = 1,
	ADC_MODE_STANDBY = 2,
	ADC_MODE_PWR_DOWN = 3,
	ADC_MODE_IDLE = 4,
	ADC_MODE_INT_OFFSET_CALIBRATION = 5,
	ADC_MODE_INT_GAIN_CALIBRATION = 6,
	ADC_MODE_SYS_OFFSET_CALIBRATION = 7,
	ADC_MODE_SYS_GAIN_CALIBRATION = 8,
};

enum class adc_pwr_mode : uint8_t {
    ADC_PWR_MODE_LOW = 0,
    ADC_PWR_MODE_MID = 1,
    ADC_PWR_MODE_FULL = 2,
};

enum class filter_type : uint8_t {
    FILTER_TYPE_SINC4 = 0, /* default */
    FILTER_TYPE_SINC3 = 2,
    FILTER_TYPE_SINC4_FAST = 4,
    FILTER_TYPE_SINC3_FAST = 5,
    FILTER_TYPE_POST_FILTER = 7,
};

enum class post_filter : uint8_t {
    post_filter_resrved = 0,
    post_filter_47hz = 2,
    post_filter_62hz = 3,
    post_filter_86hz = 5,
    post_filter_92hz = 6,
};

enum class adc_diag : uint32_t {
	DIAG_ROM_CRC = BIT(0),
	DIAG_MEM_CRC = BIT(1),
	DIAG_SPI_CRC = BIT(2),
	DIAG_SPI_WR = BIT(3),
	DIAG_SPI_RD = BIT(4),
	DIAG_SPI_SCK = BIT(5),
	DIAG_SPI_IGNORE = BIT(6),
	DIAG_ALDO_PSM = BIT(7),
	DIAG_ALDO_PSM_TRIP_TEST = BIT(8),
	DIAG_DLDO_PSM = BIT(9),
	DIAG_DLDO_PSM_TRIP_TEST = BIT(10),
	DIAG_REF_DET = BIT(11),
	DIAG_AINM_UV = BIT(12),
	DIAG_AINM_OV = BIT(13),
	DIAG_AINP_UV = BIT(14),
	DIAG_AINP_OV = BIT(15),
	DIAG_ADC_SAT = BIT(16),
	DIAG_ADC_CONV = BIT(17),
	DIAG_ADC_CAL = BIT(18),
	DIAG_LDO_CAP_CHK = BIT(19),
	DIAG_LDO_CAP_CHK_TRIP_TEST = BIT(20),
	DIAG_MCK_CNT = BIT(21),
	DIAG_MASK = 0x7F'FF'FF,
};

enum class REG_CHA: uint16_t {
	REG_CHA_EN = BIT(15),
};

struct cha_filter_param {
    filter_type type;
    bool reject_50_60Hz;
    post_filter post;
    bool single_cycle; /* only works on single analog input channel and continue conversion mode */
    uint16_t fs; /* 10:0 */
};

struct cmd {
    explicit cmd(bool wen, bool read, reg reg):
        cmd_(static_cast<uint8_t>(reg) | (wen ? 0 : BIT(7)) | (read ? BIT(6) : 0))
    {}
    uint8_t operator()() const { return cmd_; }
    uint8_t operator=(uint8_t cmd) { return cmd_; }
private:
    uint8_t cmd_;
};

struct adc {
    explicit adc(spi_iface *spi) : spi_(spi) {}

	bool initialize(void);
    bool is_alive(void);
    uint8_t read_status(void);
    bool status_is_data_ready(uint8_t status);
    uint8_t status_has_error(uint8_t status);
    uint8_t status_get_curr_cha(uint8_t status);
    bool read_data(uint32_t *data);
    uint32_t read_diag(void);
    void adc_config(adc_clock_ref clk_ref, adc_mode mode, bool internal_vol_ref, adc_pwr_mode pwr_mode = adc_pwr_mode::ADC_PWR_MODE_FULL);
    void cha_config(uint8_t ch, bool enable, const cha_filter_param &param, adc_pin_mux mux);
    void diag_config(uint32_t diag_mask);

private:
	static inline uint8_t crc8_(uint8_t *data, uint8_t len) {
		uint8_t crc = 0;

		for (uint8_t i = 0; i < len; i++) {
			crc ^= data[i];
			for (int j = 0; j < 8; j++) {
				if (crc & 0x80) {
					crc = (crc << 1) ^ 0x07;
				} else {
					crc <<= 1;
				}
			}
		}
		return crc;
	}

    template <int bytes, typename T>
    bool r_(cmd c, T* ptr, bool check_crc = false)
    {
		uint8_t tx_bf[bytes + 2] = { c() };
		uint8_t rx_bf[bytes + 2]; /* cmd:data:crc */
		T v = 0;
		uint8_t crc_expected;
		uint8_t crc;

		if (!spi_->xfer(tx_bf, rx_bf, sizeof(tx_bf), sizeof(rx_bf))) {
			return false;
		}

		for (int i = 0; i < bytes; i++) {
			v |= rx_bf[i + 1] << (8 * i);
		}
		crc_expected = rx_bf[bytes + 1];

		/* crc including cmd and data */
		rx_bf[0] = c();
		crc = crc8_(&rx_bf[0], bytes + 1);

		if (check_crc && crc != crc_expected) {
			return false;
		}

		*ptr = v;
        return true;
    }

	template <int bytes, typename T>
	void w_(cmd c, T ptr, bool check_crc = false)
	{
		uint8_t tx_buf[bytes + 2] = { c() };
		uint8_t size = sizeof(tx_buf);
		uint8_t crc;

		for (int i = 0; i < bytes; i++) {
			tx_buf[i + 1] = (ptr >> (8 * (bytes -i - 1))) & 0xFF;
		}
		if (check_crc) {
			crc = crc8_(&tx_buf[0], bytes + 1);
			tx_buf[bytes + 1] = crc;
		} else {
			size--;
		}
		spi_->xfer(tx_buf, nullptr, size, 0);
	}

    spi_iface *spi_;
	bool crc_check_;
};


} // namespace adc7124_8
