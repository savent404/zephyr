#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <string.h>

#define DMA_DEVICE_NAME DT_NODELABEL(dma0)
#define MAX_CHANNELS 8
#define BUFFER_SIZE 1024

static const struct device *const dma_dev = DEVICE_DT_GET(DMA_DEVICE_NAME);
static uint8_t src_buf[BUFFER_SIZE];
static uint8_t dst_buf[BUFFER_SIZE];
static struct dma_config dma_cfg = {0};
static struct dma_block_config dma_block = {0};
static volatile bool transfer_done = false;

static void dma_callback(const struct device *dev, void *user_data,
                        uint32_t channel, int status)
{
    transfer_done = true;
    printk("[CH-%d] Transfer completed, status: %d\n", channel, status);
}

static int test_dma_channel(uint32_t channel)
{
    printk("\n=== Testing DMA Channel %d ===\n", channel);
    
    transfer_done = false;
    
    for (int i = 0; i < BUFFER_SIZE; i++) {
        src_buf[i] = (i + channel) & 0xFF;
        dst_buf[i] = 0;
    }
    
    dma_cfg.channel_direction = MEMORY_TO_MEMORY;
    dma_cfg.source_data_size = 1;
    dma_cfg.dest_data_size = 1;
    dma_cfg.source_burst_length = 1;
    dma_cfg.dest_burst_length = 1;
    dma_cfg.dma_callback = dma_callback;
    dma_cfg.block_count = 1;
    dma_cfg.head_block = &dma_block;
    
    dma_block.block_size = BUFFER_SIZE;
    dma_block.source_address = (uint32_t)src_buf;
    dma_block.dest_address = (uint32_t)dst_buf;

    if (dma_config(dma_dev, channel, &dma_cfg)) {
        printk("[CH-%d] Configuration failed\n", channel);
        return -1;
    }

    if (dma_start(dma_dev, channel)) {
        printk("[CH-%d] Start failed\n", channel);
        return -1;
    }

    uint32_t timeout = 1000;
    while (!transfer_done && timeout--) {
        k_sleep(K_MSEC(10));
    }

    if (!transfer_done) {
        printk("[CH-%d] Transfer timeout\n", channel);
        dma_stop(dma_dev, channel);
        return -1;
    }

    if(memcmp(src_buf, dst_buf, BUFFER_SIZE)) {
        printk("[CH-%d] Data verification failed\n", channel);
        return -1;
    }

    printk("[CH-%d] Test successful\n", channel);

    return 0;
}

int main(void)
{
    printk("\n====================================\n");
    printk("          DMA Channel Test          \n");
    printk("====================================\n");
    
    if (!device_is_ready(dma_dev)) {
        printk("ERROR: DMA device not ready\n");
        return -1;
    }

    int failed_channels = 0;
    for (uint32_t channel = 0; channel < MAX_CHANNELS; channel++) {
        if (test_dma_channel(channel) != 0) {
            failed_channels++;
        }
        k_sleep(K_MSEC(100));
    }

    printk("\n====================================\n");
    printk("Test Summary:\n");
    printk("- Channels tested: %d\n", MAX_CHANNELS);
    printk("- Channels passed: %d\n", MAX_CHANNELS - failed_channels);
    printk("- Channels failed: %d\n", failed_channels);
    printk("====================================\n\n");

    return 0;
}
