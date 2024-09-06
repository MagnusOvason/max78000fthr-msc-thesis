
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
// Data input: HWC 3x32x32 (3072 bytes total / 1024 bytes per channel):
static const uint32_t input_0[] = SAMPLE_INPUT_0;
void load_input(void)
{
  // This function loads the sample data input -- replace with actual data

  int i;
  const uint32_t *in0 = input_0;

  for (i = 0; i < 1024; i++)
  {
    // Remove the following line if there is no risk that the source would overrun the FIFO:
    while (((*((volatile uint32_t *)0x50000004) & 1)) != 0)
      ;                                          // Wait for FIFO 0
    *((volatile uint32_t *)0x50000008) = *in0++; // Write FIFO 0
  }
}
#else
#define CAMERA_FREQ (5 * 1000 * 1000) // 5 MHz
#define IMAGE_XRES (32)               // X resolution
#define IMAGE_YRES (32)               // Y resolution

// Data input: HWC 3x32x32 (3072 bytes total / 1024 bytes per channel): IMAGE_XRES * IMAGE_YRES = 32 * 32 = 1024
static uint32_t input_from_camera[IMAGE_XRES * IMAGE_YRES];
void load_input(void)
{
  int i;
  const uint32_t *in0 = input_from_camera;

  for (i = 0; i < (IMAGE_XRES * IMAGE_YRES); i++)
  {
    // Remove the following line if there is no risk that the source would overrun the FIFO:
    while (((*((volatile uint32_t *)0x50000004) & 1)) != 0)
      ;                                          // Wait for FIFO 0
    *((volatile uint32_t *)0x50000008) = *in0++; // Write FIFO 0
  }
}

void capture_process_camera(void)
{
  uint8_t *rawData;
  uint32_t imageLength, imageWidth, imageHeight;
  uint8_t *data = NULL;
  uint8_t r, g, b;
  stream_stat_t *stat;
  int cnt = 0;

  camera_start_capture_image(); // Start camera capture

  camera_get_image(&rawData, &imageLength, &imageWidth, &imageHeight); // Get image from camera
  printf("\nRaw Data: %d\n", rawData);
  printf("Image Length: %d\n", imageLength);
  printf("Image Width: %d\n", imageWidth);
  printf("Image Height: %d\n", imageHeight);

  // Get image line by line
  for (int row = 0; row < imageHeight; row++)
  {
    // Wait until camera streaming buffer is full
    while ((data = get_camera_stream_buffer()) == NULL)
    {
      if (camera_is_image_rcv())
      {
        break;
      }
    }

    for (int k = 0; k < 4 * imageWidth; k += 4)
    {
      // data format: 0x00bbggrr
      r = data[k];
      g = data[k + 1];
      b = data[k + 2];
      // skip k+3

      // change the range from [0,255] to [-128,127] and store in buffer for CNN
      // input_from_camera[cnt++] = ((b << 16) | (g << 8) | r) ^ 0x00808080;
      input_from_camera[cnt] = ((b << 16) | (g << 8) | r) ^ 0x00808080;
      cnt++;

      release_camera_stream_buffer(); // Release camera streaming buffer
    }

    stat = get_camera_stream_statistic();

    if (stat->overflow_count > 0)
    {
      printf("OVERFLOW DISP = %d\n", stat->overflow_count);
      LED_On(LED_RED); // Turn on red LED if overflow detected
      while (1)
      {
      }
    }
  }

  return;
}
#endif

#if !USE_CAMERA
// Expected output of layer 10 for cifar-10-max78000fthr-nas given the sample input (known-answer test)
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
  MXC_ICC_Enable(MXC_ICC0); // Enable cache

  // Switch to 100 MHz clock
  // MXC_SYS_Clock_Select(MXC_SYS_CLOCK_IPO); // 100 MHz
  // MXC_SYS_Clock_Select(MXC_SYS_CLOCK_ISO); // 60 MHz
  MXC_SYS_Clock_Select(MXC_SYS_CLOCK_IBRO); // 7.3728 MHz
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

    printf("\n*** CNN Inference Test cifar-10-max78000fthr-nas ***\n");

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
#define CAMERA_FREQ (5 * 1000 * 1000) // 5 MHz
#define IMAGE_XRES (32)               // X resolution
#define IMAGE_YRES (32)               // Y resolution

  // Initialize DMA for camera interface
  MXC_DMA_Init();
  dma_channel = MXC_DMA_AcquireChannel();
  camera_init(CAMERA_FREQ);
  ret = camera_setup(IMAGE_XRES, IMAGE_YRES, PIXFORMAT_RGB888, FIFO_THREE_BYTE, STREAMING_DMA,
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
  // MXC_SYS_Clock_Select(MXC_SYS_CLOCK_IPO); // 100 MHz
  // MXC_SYS_Clock_Select(MXC_SYS_CLOCK_ISO); // 60 MHz
  MXC_SYS_Clock_Select(MXC_SYS_CLOCK_IBRO); // 7.3728MHz
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
  Hardware: 36,481,536 ops (36,180,992 macc; 300,544 comp; 0 add; 0 mul; 0 bitwise)
    Layer 0: 1,835,008 ops (1,769,472 macc; 65,536 comp; 0 add; 0 mul; 0 bitwise)
    Layer 1: 2,129,920 ops (2,097,152 macc; 32,768 comp; 0 add; 0 mul; 0 bitwise)
    Layer 2: 18,939,904 ops (18,874,368 macc; 65,536 comp; 0 add; 0 mul; 0 bitwise)
    Layer 3: 4,792,320 ops (4,718,592 macc; 73,728 comp; 0 add; 0 mul; 0 bitwise)
    Layer 4: 540,672 ops (524,288 macc; 16,384 comp; 0 add; 0 mul; 0 bitwise)
    Layer 5: 4,743,168 ops (4,718,592 macc; 24,576 comp; 0 add; 0 mul; 0 bitwise)
    Layer 6: 1,056,768 ops (1,048,576 macc; 8,192 comp; 0 add; 0 mul; 0 bitwise)
    Layer 7: 1,188,864 ops (1,179,648 macc; 9,216 comp; 0 add; 0 mul; 0 bitwise)
    Layer 8: 1,181,696 ops (1,179,648 macc; 2,048 comp; 0 add; 0 mul; 0 bitwise)
    Layer 9: 68,096 ops (65,536 macc; 2,560 comp; 0 add; 0 mul; 0 bitwise)
    Layer 10: 5,120 ops (5,120 macc; 0 comp; 0 add; 0 mul; 0 bitwise)

  RESOURCE USAGE
  Weight memory: 301,760 bytes out of 442,368 bytes total (68.2%)
  Bias memory:   842 bytes out of 2,048 bytes total (41.1%)
*/
