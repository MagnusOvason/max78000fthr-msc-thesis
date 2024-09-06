#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "mxc.h"
#include "cnn.h"
#include "sampledata.h"
#include "sampleoutput.h"
#include "camera.h"
#include "dma.h"
#include "board.h"

#define DISABLE_GPIO 1 // Disable GPIOs to save power

// Switch between active and sleep mode by setting DO_ACTIVE or DO_SLEEP.
// #define DO_ACTIVE 0
// #define DO_SLEEP 1
// #define DO_LPM 0
// #define DO_UPM 0
// #define DO_STANDBY 0
// #define DO_BACKUP 0 // will reset after wakeup

#define USE_ALARM 1 
#define DELAY_IN_SEC 4

#define USE_CAMERA 1

#if USE_ALARM
volatile int alarmed;
void alarmHandler(void)
{
  int flags = MXC_RTC->ctrl;
  alarmed = 1;

  if ((flags & MXC_F_RTC_CTRL_SSEC_ALARM) >> MXC_F_RTC_CTRL_SSEC_ALARM_POS)
  {
    MXC_RTC->ctrl &= ~(MXC_F_RTC_CTRL_SSEC_ALARM);
  }

  if ((flags & MXC_F_RTC_CTRL_TOD_ALARM) >> MXC_F_RTC_CTRL_TOD_ALARM_POS)
  {
    MXC_RTC->ctrl &= ~(MXC_F_RTC_CTRL_TOD_ALARM);
  }
}

void setTrigger(int waitForTrigger)
{
  alarmed = 0;

  while (MXC_RTC_Init(0, 0) == E_BUSY)
  {
  }

  while (MXC_RTC_DisableInt(MXC_F_RTC_CTRL_TOD_ALARM_IE) == E_BUSY)
  {
  }

  while (MXC_RTC_SetTimeofdayAlarm(DELAY_IN_SEC) == E_BUSY)
  {
  }

  while (MXC_RTC_EnableInt(MXC_F_RTC_CTRL_TOD_ALARM_IE) == E_BUSY)
  {
  }

  while (MXC_RTC_Start() == E_BUSY)
  {
  }

  if (waitForTrigger)
  {
    while (!alarmed)
    {
    }
  }
}
#endif // USE_ALARM

volatile uint32_t cnn_time; // Stopwatch

void fail(void)
{
  printf("\n*** FAIL ***\n\n");
  while (1)
    ;
}

#if !USE_CAMERA
// Data input: HWC 3x128x128 (49152 bytes total / 16384 bytes per channel):
static const uint32_t input_0[] = SAMPLE_INPUT_0;
void load_input(void)
{
  // This function loads the sample data input -- replace with actual data

  int i;
  const uint32_t *in0 = input_0;

  for (i = 0; i < 16384; i++)
  {
    // Remove the following line if there is no risk that the source would overrun the FIFO:
    while (((*((volatile uint32_t *)0x50000004) & 1)) != 0)
      ;                                          // Wait for FIFO 0
    *((volatile uint32_t *)0x50000008) = *in0++; // Write FIFO 0
  }
}
#else
#define IMAGE_SIZE_X (64 * 2) // 128
#define IMAGE_SIZE_Y (64 * 2) // 128
#define CAMERA_FREQ (5 * 1000 * 1000)

// RGB565 buffer for TFT
uint8_t data565[IMAGE_SIZE_X * 2];

// Buffer for camera image
static uint32_t input_0[IMAGE_SIZE_X * IMAGE_SIZE_Y]; // buffer for camera image

// DMA channel for camera interface
int dma_channel;
int g_dma_channel_tft = 1;
void load_input(void)
{
  int i;                         // counter
  const uint32_t *in0 = input_0; // input buffer

  for (i = 0; i < 16384; i++)
  {
    // Remove the following line if there is no risk that the source would overrun the FIFO:
    while (((*((volatile uint32_t *)0x50000004) & 1)) != 0)
    {
    }
    // Wait for FIFO 0
    *((volatile uint32_t *)0x50000008) = *in0++; // Write FIFO 0
  }
}

// the function to capture image from camera. What it does is to get the image from camera and store it in the buffer. Then, it will convert the image to RGB565 format for display.
void capture_process_camera(void)
{
  uint8_t *raw;
  uint32_t imgLen;
  uint32_t w, h;

  int cnt = 0;

  uint8_t r, g, b;
  uint16_t rgb;
  int j = 0;

  uint8_t *data = NULL;
  stream_stat_t *stat;

  camera_start_capture_image();

  // Get the details of the image from the camera driver.
  camera_get_image(&raw, &imgLen, &w, &h);
  printf("\nW:%d H:%d L:%d \n", w, h, imgLen);

  // Get image line by line
  for (int row = 0; row < h; row++)
  {
    // Wait until camera streaming buffer is full
    while ((data = get_camera_stream_buffer()) == NULL)
    {
      if (camera_is_image_rcv())
      {
        break;
      }
    }

    for (int k = 0; k < 4 * w; k += 4)
    {
      // data format: 0x00bbggrr
      r = data[k];
      g = data[k + 1];
      b = data[k + 2];
      // skip k+3 as it is not used.

      // change the range from [0,255] to [-128,127] and store in buffer for CNN
      input_0[cnt++] = ((b << 16) | (g << 8) | r) ^ 0x00808080;


      rgb = ((r & 0b11111000) << 8) | ((g & 0b11111100) << 3) | (b >> 3);
      data565[j] = (rgb >> 8) & 0xFF;
      data565[j + 1] = rgb & 0xFF;
    }

    // LED_Toggle(LED2);
    //  Release stream buffer
    release_camera_stream_buffer();
  }

  // camera_sleep(1);
  stat = get_camera_stream_statistic();

  if (stat->overflow_count > 0)
  {
    printf("OVERFLOW DISP = %d\n", stat->overflow_count);
    while (1)
    {
    }
  }
}
#endif

#if !USE_CAMERA
// Expected output of layer 6 for cats-dogs-max78000fthr given the sample input (known-answer test)
// Delete this function for production code
static const uint32_t sample_output[] = SAMPLE_OUTPUT;
int check_output(void)
{
  int i;
  uint32_t mask, len;
  volatile uint32_t *addr;
  const uint32_t *ptr = sample_output;

  while ((addr = (volatile uint32_t *)*ptr++) != 0)
  {
    mask = *ptr++;
    len = *ptr++;
    for (i = 0; i < len; i++)
      if ((*addr++ & mask) != *ptr++)
      {
        printf("Data mismatch (%d/%d) at address 0x%08x: Expected 0x%08x, read 0x%08x.\n",
               i + 1, len, addr - 1, *(ptr - 1), *(addr - 1) & mask);
        return CNN_FAIL;
      }
  }

  return CNN_OK;
}
#endif

// Classification layer:
static int32_t ml_data[CNN_NUM_OUTPUTS];
static q15_t ml_softmax[CNN_NUM_OUTPUTS];

void softmax_layer(void)
{
  cnn_unload((uint32_t *)ml_data);
  softmax_q17p14_q15((const q31_t *)ml_data, CNN_NUM_OUTPUTS, ml_softmax);
}

int main(void)
{
  int i;
  int digs, tens;
  int mode = 0;
#if USE_CAMERA
  int ret;
  int dma_channel;
#endif

#if USE_ALARM
  printf("This code cycles through the MAX78000 power modes, using the RTC alarm to exit from "
         "each mode.  The modes will change every %d seconds.\n\n",
         DELAY_IN_SEC);
  MXC_NVIC_SetVector(RTC_IRQn, alarmHandler); // Set RTC alarm handler
#endif

#if !USE_CAMERA
#if DISABLE_GPIO
  // To save power, configure all GPIOs as input, only keep console (or LEDs)
  mxc_gpio_cfg_t gpios_in;

  // all GPIOs input with pullup
  gpios_in.pad = MXC_GPIO_PAD_PULL_UP;
  gpios_in.func = MXC_GPIO_FUNC_IN;
  gpios_in.vssel = MXC_GPIO_VSSEL_VDDIO;
  gpios_in.drvstr = MXC_GPIO_DRVSTR_0;

  // PORT3 input
  gpios_in.port = MXC_GPIO3;
  gpios_in.mask = 0xFFFFFFFF;
  MXC_GPIO_Config(&gpios_in);

  // PORT2 input
  gpios_in.port = MXC_GPIO2;
  MXC_GPIO_Config(&gpios_in);

  // PORT1 input
  gpios_in.port = MXC_GPIO1;
  MXC_GPIO_Config(&gpios_in);

  // PORT0 input except consule
  gpios_in.port = MXC_GPIO0;
  gpios_in.mask = 0xFFFFFFFD; // except UART0-TX for debug
  // gpios_in.mask = 0xFFFFFFF3;   // except LEDs (slightly higher power)
  MXC_GPIO_Config(&gpios_in);
#endif
#endif

#if USE_ALARM
  MXC_LP_EnableRTCAlarmWakeup();
#endif // USE_ALARM

#if !USE_CAMERA
  // Enable cache
  MXC_ICC_Enable(MXC_ICC0); // Enable cache

  // Clock selection:
  MXC_SYS_Clock_Select(MXC_SYS_CLOCK_IPO); // 100 MHz
  // MXC_SYS_Clock_Select(MXC_SYS_CLOCK_ISO); // 60 MHz
  // MXC_SYS_Clock_Select(MXC_SYS_CLOCK_IBRO); // 7.3728 MHz
  SystemCoreClockUpdate();

  while (1)
  {

    if (mode == 0)
    {
      printf("\nActive mode\n");
      setTrigger(1);
      // Don't need to do anything, already in active mode
      mode = 1;
    }
    else if (mode == 1)
    {
      printf("\nSleep mode\n");
      setTrigger(0);
      MXC_LP_EnterSleepMode();
      mode = 2;
    }
    else if (mode == 2)
    {
      printf("\nLow Power mode\n");
      setTrigger(0);
      MXC_LP_EnterLowPowerMode();
      mode = 3;
    }
    else if (mode == 3)
    {
      printf("\nUltra Low Power mode\n");
      setTrigger(0);
      MXC_LP_EnterMicroPowerMode();
      mode = 4;
    }
    else if (mode == 4)
    {
      printf("\nStandby mode\n");
      setTrigger(0);
      MXC_LP_EnterStandbyMode();
      mode = 5;
    }
    else if (mode == 5)
    {
      printf("\nBackup mode\n");
      setTrigger(0);
      MXC_LP_EnterBackupMode();
      mode = 0;
    }

    printf("Waiting...\n");

    // DO NOT DELETE THIS LINE:
    MXC_Delay(SEC(2)); // Let debugger interrupt if needed

    // Enable peripheral, enable CNN interrupt, turn on CNN clock
    // CNN clock: APB (50 MHz) div 1
    cnn_enable(MXC_S_GCR_PCLKDIV_CNNCLKSEL_PCLK, MXC_S_GCR_PCLKDIV_CNNCLKDIV_DIV1);

    printf("\n*** CNN Inference Test cats-dogs-max78000fthr ***\n");

    cnn_init();         // Bring state machine into consistent state
    cnn_load_weights(); // Load kernels
    cnn_load_bias();
    cnn_configure(); // Configure state machine
    cnn_start();     // Start CNN processing
    load_input();    // Load data input via FIFO

    while (cnn_time == 0)
      MXC_LP_EnterSleepMode(); // Wait for CNN

    if (check_output() != CNN_OK)
      fail();
    softmax_layer();

    printf("\n*** PASS ***\n\n");

#ifdef CNN_INFERENCE_TIMER
    printf("Approximate data loading and inference time: %u us\n\n", cnn_time);
#endif

    cnn_disable(); // Shut down CNN clock, disable peripheral

    printf("Classification results:\n");
    for (i = 0; i < CNN_NUM_OUTPUTS; i++)
    {
      digs = (1000 * ml_softmax[i] + 0x4000) >> 15;
      tens = digs % 10;
      digs = digs / 10;
      printf("[%7d] -> Class %d: %d.%d%%\n", ml_data[i], i, digs, tens);
    }
  }
#else
  // Initialize DMA for camera interface
  MXC_DMA_Init();
  dma_channel = MXC_DMA_AcquireChannel();
  camera_init(CAMERA_FREQ);
  ret = camera_setup(IMAGE_SIZE_X, IMAGE_SIZE_Y, PIXFORMAT_RGB888, FIFO_THREE_BYTE, STREAMING_DMA,
                     dma_channel);
  if (ret != STATUS_OK)
  {
    printf("Error returned from setting up camera. Error %d\n", ret);
    return -1;
  }
  camera_write_reg(0x11, 0x0); // Set the camera to sleep mode

  // Enable cache
  MXC_ICC_Enable(MXC_ICC0); // Enable cache

  // Clock selection:
  MXC_SYS_Clock_Select(MXC_SYS_CLOCK_IPO); // 100 MHz
  // MXC_SYS_Clock_Select(MXC_SYS_CLOCK_ISO); // 60 MHz
  // MXC_SYS_Clock_Select(MXC_SYS_CLOCK_IBRO); // 7.3728MHz
  SystemCoreClockUpdate();

  Camera_Power(POWER_OFF);

  while (1)
  {
    if (mode == 0)
    {
      printf("\nActive mode\n");
      setTrigger(1);
      // Don't need to do anything, already in active mode
      mode = 1;
    }
    else if (mode == 1)
    {
      printf("\nSleep mode\n");
      setTrigger(0);
      MXC_LP_EnterSleepMode();
      mode = 2;
    }
    else if (mode == 2)
    {
      printf("\nLow Power mode\n");
      setTrigger(0);
      MXC_LP_EnterLowPowerMode();
      mode = 3;
    }
    else if (mode == 3)
    {
      printf("\nUltra Low Power mode\n");
      setTrigger(0);
      MXC_LP_EnterMicroPowerMode();
      mode = 4;
    }
    else if (mode == 4)
    {
      printf("\nStandby mode\n");
      setTrigger(0);
      MXC_LP_EnterStandbyMode();
      mode = 5;
    }
    else if (mode == 5)
    {
      printf("\nBackup mode\n");
      setTrigger(0);
      MXC_LP_EnterBackupMode();
      mode = 0;
    }

    Camera_Power(POWER_ON);

    printf("Waiting...\n");

    // DO NOT DELETE THIS LINE:
    MXC_Delay(SEC(2)); // Let debugger interrupt if needed

    // Enable peripheral, enable CNN interrupt, turn on CNN clock
    // CNN clock: APB (50 MHz) div 1
    cnn_enable(MXC_S_GCR_PCLKDIV_CNNCLKSEL_PCLK, MXC_S_GCR_PCLKDIV_CNNCLKDIV_DIV1);

    /* Configure P2.5, turn on the CNN Boost */
    // cnn_boost_enable(MXC_GPIO2, MXC_GPIO_PIN_5);

    cnn_init();         // Bring state machine into consistent state
    cnn_load_weights(); // Load kernels
    cnn_load_bias();
    cnn_configure(); // Configure state machine

    capture_process_camera();
    Camera_Power(POWER_OFF);

    cnn_start();  // Start CNN processing
    load_input(); // Load data input via FIFO

    while (cnn_time == 0)
      MXC_LP_EnterSleepMode(); // Wait for CNN

    softmax_layer();

    printf("\n*** PASS ***\n\n");

#ifdef CNN_INFERENCE_TIMER
    printf("Approximate data loading and inference time: %u us\n\n", cnn_time);
#endif

    cnn_disable(); // Shut down CNN clock, disable peripheral

    printf("Classification results:\n");
    for (i = 0; i < CNN_NUM_OUTPUTS; i++)
    {
      digs = (1000 * ml_softmax[i] + 0x4000) >> 15;
      tens = digs % 10;
      digs = digs / 10;
      printf("[%7d] -> Class %d: %d.%d%%\n", ml_data[i], i, digs, tens);
    }
  }
#endif
  return 0;
}

/*
  SUMMARY OF OPS
  Hardware: 51,368,960 ops (50,432,000 macc; 936,960 comp; 0 add; 0 mul; 0 bitwise)
    Layer 0: 7,340,032 ops (7,077,888 macc; 262,144 comp; 0 add; 0 mul; 0 bitwise)
    Layer 1: 19,267,584 ops (18,874,368 macc; 393,216 comp; 0 add; 0 mul; 0 bitwise)
    Layer 2: 19,070,976 ops (18,874,368 macc; 196,608 comp; 0 add; 0 mul; 0 bitwise)
    Layer 3: 4,792,320 ops (4,718,592 macc; 73,728 comp; 0 add; 0 mul; 0 bitwise)
    Layer 4: 600,064 ops (589,824 macc; 10,240 comp; 0 add; 0 mul; 0 bitwise)
    Layer 5: 295,936 ops (294,912 macc; 1,024 comp; 0 add; 0 mul; 0 bitwise)
    Layer 6: 2,048 ops (2,048 macc; 0 comp; 0 add; 0 mul; 0 bitwise)

  RESOURCE USAGE
  Weight memory: 57,776 bytes out of 442,368 bytes total (13.1%)
  Bias memory:   2 bytes out of 2,048 bytes total (0.1%)
*/
