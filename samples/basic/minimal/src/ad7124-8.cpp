#include "ad7124-8.hpp"
#include <zephyr/kernel.h>

using namespace adc7124_8;

bool adc::is_alive(void)
{
    uint8_t res = 0;

    /* This function will be called no matter crc_check_ is true or false */
    r_<1>(cmd{true, true, reg::REG_ID}, &res, false);
    return res == 0x17;
}

bool adc::initialize(void)
{
    uint32_t reg_diag;

    /* read back some register to check if driver is out of track */
    r_<3>(cmd{true, true, reg::REG_ERR_EN}, &reg_diag, false);
    if (reg_diag & static_cast<uint8_t>(adc_diag::DIAG_SPI_CRC)) {
        crc_check_ = true;
    } else {
        crc_check_ = false;
    }

    return true;
}

bool adc::get_status(uint8_t *status)
{
    return r_<1>(cmd{true, true, reg::REG_STATUS}, status, false);
}

bool adc::status_is_data_ready(uint8_t status)
{
    return status & static_cast<uint8_t>(status::STATUS_RDY_N) ? false : true;
}

uint8_t adc::status_has_error(uint8_t status)
{
    const uint8_t err_mask = static_cast<uint8_t>(status::STATUS_ERR) | static_cast<uint8_t>(status::STATUS_POR);
    return status & err_mask;
}

uint8_t adc::status_get_curr_cha(uint8_t status)
{
    return status & 0x0F;
}

bool adc::get_data(uint32_t *data)
{
    return r_<3>(cmd{true, true, reg::REG_DATA}, data, crc_check_);
}

bool adc::get_diag(uint32_t *diag)
{
    return r_<3>(cmd{true, true, reg::REG_ERR}, diag, crc_check_);
}

void adc::adc_config(adc_clock_ref clk_ref, adc_mode mode, bool internal_vol_ref, adc_pwr_mode pwr_mode)
{
    uint8_t p = 0;
    p |= (static_cast<uint8_t>(clk_ref) << 0);
    p |= ((static_cast<uint8_t>(mode) & 0x7) << 2);
    p |= ((static_cast<uint8_t>(pwr_mode) & 0x3) << 6);
    p |= (internal_vol_ref ? 0 : BIT(8));
    w_<1>(cmd{true, false, reg::REG_ADC_CTRL}, p, crc_check_);
}

void adc::cha_config(uint8_t ch, bool enable, const cha_filter_param &param, adc_pin_mux mux)
{

    cmd c{true, false, (reg)((uint8_t)reg::REG_CHA_0 + ch)};
    uint16_t p = 0;

    if (ch > 8) {
        return;
    }
    if (enable) {
        p |= static_cast<uint16_t>(REG_CHA::REG_CHA_EN);
    }
    p |= (ch << 12); /* use different setup as default */
    if (mux == adc_pin_mux::ADC_PIN_MUX_DIFF_AUTO) {
        p |= (ch*2 << 5);
        p |= (ch*2 + 1);
    } else {
        uint8_t _c = static_cast<uint8_t>(mux) & 0x7;
        p |= (_c << 5);
        p |= (_c + 1);
    }
    w_<2>(cmd{true, false, (reg)((uint8_t)reg::REG_CHA_0 + ch)}, p, crc_check_);

    cmd f{true, false, (reg)((uint8_t)reg::REG_FILTER_0 + ch)};
    uint32_t f_val = 0;
    f_val |= param.fs & 0x3FF; /* output data rate */
    f_val |= (param.single_cycle ? BIT(16) : 0);
    f_val |= (static_cast<uint8_t>(param.post) & 0x7) << 17;
    f_val |= (param.reject_50_60Hz ? BIT(20) : 0);
    f_val |= (static_cast<uint8_t>(param.type) & 0x7) << 21;
    w_<3>(f, f_val, crc_check_);
}

void adc::diag_config(uint32_t diag_mask)
{
    w_<3>(cmd{true, false, reg::REG_ERR_EN}, diag_mask & 0x7F'FF'FF, crc_check_);

    if (diag_mask & static_cast<uint32_t>(adc_diag::DIAG_SPI_CRC)) {
        crc_check_ = true;
        printk("CRC check enabled\n");
    } else {
        crc_check_ = false;
        printk("CRC check disabled\n");
    }
}

