#include "app_main.h"

#include "cdc_uart.hpp"
#include "libxr.hpp"
#include "main.h"
#include "stm32_adc.hpp"
#include "stm32_can.hpp"
#include "stm32_canfd.hpp"
#include "stm32_dac.hpp"
#include "stm32_flash.hpp"
#include "stm32_gpio.hpp"
#include "stm32_i2c.hpp"
#include "stm32_power.hpp"
#include "stm32_pwm.hpp"
#include "stm32_spi.hpp"
#include "stm32_timebase.hpp"
#include "stm32_uart.hpp"
#include "stm32_usb_dev.hpp"
#include "stm32_watchdog.hpp"
#include "flash_map.hpp"

using namespace LibXR;

/* User Code Begin 1 */
/* User Code End 1 */
// NOLINTBEGIN
// clang-format off
/* External HAL Declarations */
extern FDCAN_HandleTypeDef hfdcan1;
extern FDCAN_HandleTypeDef hfdcan2;
extern FDCAN_HandleTypeDef hfdcan3;
extern PCD_HandleTypeDef hpcd_USB_DEVICE;
extern PCD_HandleTypeDef hpcd_USB_OTG_HS;
extern SPI_HandleTypeDef hspi2;
extern SPI_HandleTypeDef hspi6;
extern TIM_HandleTypeDef htim12;
extern TIM_HandleTypeDef htim1;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern UART_HandleTypeDef huart5;
extern UART_HandleTypeDef huart6;
extern UART_HandleTypeDef huart7;
extern UART_HandleTypeDef huart8;

/* DMA Resources */
static uint8_t spi2_tx_buf[32];
static uint8_t spi2_rx_buf[32];
static uint8_t usart1_tx_buf[128];
static uint8_t usart1_rx_buf[128];
static uint8_t usart2_tx_buf[128];
static uint8_t usart2_rx_buf[128];
static uint8_t usart3_tx_buf[128];
static uint8_t usart3_rx_buf[128];
static uint8_t usart6_tx_buf[128];
static uint8_t usart6_rx_buf[128];
static uint8_t uart5_tx_buf[128];
static uint8_t uart5_rx_buf[128];
static uint8_t uart8_tx_buf[128];
static uint8_t uart8_rx_buf[128];
static uint8_t uart7_tx_buf[128];
static uint8_t uart7_rx_buf[128];

extern "C" void app_main(void) {
  // clang-format on
  // NOLINTEND
  /* User Code Begin 2 */
  
  /* User Code End 2 */
  // clang-format off
  // NOLINTBEGIN
  STM32TimerTimebase timebase(&htim1);
  PlatformInit(2, 1024);
  STM32PowerManager power_manager;

  /* GPIO Configuration */
  STM32GPIO CS1_ACCEL(CS1_ACCEL_GPIO_Port, CS1_ACCEL_Pin);
  STM32GPIO CS1_GYRO(CS1_GYRO_GPIO_Port, CS1_GYRO_Pin);
  STM32GPIO INT1_ACCEL(INT1_ACCEL_GPIO_Port, INT1_ACCEL_Pin, EXTI15_10_IRQn);
  STM32GPIO INT1_GYRO(INT1_GYRO_GPIO_Port, INT1_GYRO_Pin, EXTI15_10_IRQn);


  STM32PWM pwm_tim12_ch2(&htim12, TIM_CHANNEL_2, false);

  STM32SPI spi2(&hspi2, spi2_rx_buf, spi2_tx_buf, 3);

  STM32SPI spi6(&hspi6, {nullptr, 0}, {nullptr, 0}, 3);

  STM32UART usart1(&huart1,
              usart1_rx_buf, usart1_tx_buf, 5);

  STM32UART usart2(&huart2,
              usart2_rx_buf, usart2_tx_buf, 5);

  STM32UART usart3(&huart3,
              usart3_rx_buf, usart3_tx_buf, 5);

  STM32UART usart6(&huart6,
              usart6_rx_buf, usart6_tx_buf, 5);

  STM32UART uart5(&huart5,
              uart5_rx_buf, uart5_tx_buf, 5);

  STM32UART uart8(&huart8,
              uart8_rx_buf, uart8_tx_buf, 5);

  STM32UART uart7(&huart7,
              uart7_rx_buf, uart7_tx_buf, 5);

  STM32CANFD fdcan1(&hfdcan1, 5);

  STM32CANFD fdcan2(&hfdcan2, 5);

  STM32CANFD fdcan3(&hfdcan3, 5);

  /* Terminal Configuration */

  // clang-format on
  // NOLINTEND
  /* User Code Begin 3 */
  while(true) {
    Thread::Sleep(UINT32_MAX);
  }
  /* User Code End 3 */
}