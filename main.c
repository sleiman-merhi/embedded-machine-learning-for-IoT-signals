#include "main.h"
#include "adc.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* USER CODE BEGIN Includes */
#include "NanoEdgeAI.h"          // NanoEdge AI inference library
#include <stdio.h>               // printf support
/* USER CODE END Includes */

/* USER CODE BEGIN PD */
#define BH1750_ADDR  (0x23 << 1) // BH1750 I2C address left-shifted for HAL
#define DHT22_PIN     GPIO_PIN_5  // DHT22 data pin
#define DHT22_PORT    GPIOB       // DHT22 data port
/* USER CODE END PD */

/* USER CODE BEGIN PV */
extern UART_HandleTypeDef hcom_uart[]; // BSP UART handle for COM1
COM_InitTypeDef BspCOMInit;            // BSP COM port configuration structure

/* NanoEdge AI buffers — sizes defined by exported NanoEdgeAI.h constants */
float input_buffer[NEAI_INPUT_SIGNAL_LENGTH * NEAI_INPUT_AXIS_NUMBER]; // 6 sensor values input
float output_buffer[NEAI_NUMBER_OF_CLASSES];                           // 8 class probabilities output
int id_class = 0;                                                       // predicted class index

/* Sensor reading variables */
float co2=0.0f, humidity=0.0f, light=0.0f;
float motion=0.0f, smoke=0.0f, temperature=0.0f;

/* DHT22 raw byte variables */
uint8_t Rh_byte1, Rh_byte2, Temp_byte1, Temp_byte2;
uint16_t SUM, RH, TEMP;
uint8_t Presence = 0; // DHT22 response flag
/* USER CODE END PV */

void SystemClock_Config(void); // Forward declaration

/* USER CODE BEGIN 0 */

// TIM2-based microsecond delay — resets counter and waits until target count reached
void delay(uint16_t time) {
    __HAL_TIM_SET_COUNTER(&htim2, 0);
    while((__HAL_TIM_GET_COUNTER(&htim2)) < time);
}

// Redirects printf output to COM1 UART for Tera Term serial monitoring
int _write(int file, char *ptr, int len) {
    HAL_UART_Transmit(&hcom_uart[COM1], (uint8_t*)ptr, len, 1000);
    return len;
}

// Reads a single ADC channel — stops any ongoing conversion, configures channel,
// starts conversion, waits for result, returns 12-bit raw value (0-4095)
uint32_t Read_ADC(uint32_t channel) {
    HAL_ADC_Stop(&hadc);
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_160CYCLES_5;
    if(HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK) return 0;
    if(HAL_ADC_Start(&hadc) != HAL_OK) return 0;
    if(HAL_ADC_PollForConversion(&hadc, 1000) != HAL_OK) return 0;
    uint32_t val = HAL_ADC_GetValue(&hadc);
    HAL_ADC_Stop(&hadc);
    return val;
}

// Sends continuous high-resolution mode command (0x10) to BH1750 over I2C
// then waits 200ms for sensor to stabilize
void BH1750_Init(void) {
    uint8_t cmd = 0x10;
    HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1, 100);
    HAL_Delay(200);
}

// Reads 2 bytes from BH1750, combines into 16-bit raw value,
// divides by 1.2 to convert to lux
float BH1750_Read(void) {
    uint8_t data[2];
    HAL_I2C_Master_Receive(&hi2c1, BH1750_ADDR, data, 2, 100);
    uint16_t raw = (data[0] << 8) | data[1];
    return raw / 1.2f;
}

// Configures DHT22 pin as push-pull output for sending start signal
void Set_Pin_Output(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}

// Configures DHT22 pin as floating input for receiving sensor response
void Set_Pin_Input(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}

// Sends DHT22 start signal: pulls line low for 1200us, releases high for 20us,
// then switches pin to input mode to receive sensor response
void DHT22_Start(void) {
    Set_Pin_Output(DHT22_PORT, DHT22_PIN);
    HAL_GPIO_WritePin(DHT22_PORT, DHT22_PIN, 0);
    delay(1200);
    HAL_GPIO_WritePin(DHT22_PORT, DHT22_PIN, 1);
    delay(20);
    Set_Pin_Input(DHT22_PORT, DHT22_PIN);
}

// Verifies DHT22 acknowledgment: waits 40us then checks if sensor pulled line low,
// waits 80us then checks if sensor pulled line high — returns 1 on valid response
uint8_t DHT22_Check_Response(void) {
    uint8_t Response = 0;
    uint32_t t = 0;
    delay(40);
    if(!(HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN))) {
        delay(80);
        if(HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN)) Response = 1;
        else Response = -1;
    }
    t = 0;
    while((HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN)) && t++ < 5000); // wait for line to go low
    return Response;
}

// Reads one byte (8 bits) from DHT22 — for each bit waits for line to go high,
// samples after 40us delay: if still high bit=1, if low bit=0
// timeout counter prevents infinite blocking if sensor stops responding
uint8_t DHT22_Read(void) {
    uint8_t i = 0, j;
    for(j = 0; j < 8; j++) {
        uint32_t t = 0;
        while(!(HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN)) && t++ < 5000); // wait for high
        delay(40);                                                         // sample point
        if(!(HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN)))
            i &= ~(1 << (7-j));  // bit = 0
        else
            i |= (1 << (7-j));   // bit = 1
        t = 0;
        while((HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN)) && t++ < 5000); // wait for low
    }
    return i;
}
/* USER CODE END 0 */

int main(void)
{
    // Initialize HAL, system clock, and all configured peripherals
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_ADC_Init();
    MX_I2C1_Init();
    MX_TIM2_Init();
    MX_USART2_UART_Init();

    /* USER CODE BEGIN 2 */
    HAL_TIM_Base_Start(&htim2); // Start TIM2 for microsecond delay function
    BH1750_Init();              // Initialize BH1750 light sensor

    // Initialize NanoEdge AI library — loads model knowledge into RAM
    enum neai_state error_code = neai_classification_init();
    if(error_code != NEAI_OK) {
        printf("NEAI Init Failed!\r\n");
        while(1); // halt if model fails to load
    }

    // Configure and initialize BSP COM1 for serial output at 115200 baud
    BspCOMInit.BaudRate   = 115200;
    BspCOMInit.WordLength = COM_WORDLENGTH_8B;
    BspCOMInit.StopBits   = COM_STOPBITS_1;
    BspCOMInit.Parity     = COM_PARITY_NONE;
    BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
    if(BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE) Error_Handler();

    HAL_PWREx_ReleaseCore(PWR_CORE_CPU2); // Boot Cortex-M0+ core
    /* USER CODE END 2 */

    printf("CO2 | Hum | Light | Mot | Smoke | Temp | Classification\r\n"); // CSV header

    while(1)
    {
        /* ── 1. DATA ACQUISITION ─────────────────────────────────────────── */
        DHT22_Start(); // Send start signal to DHT22
        if(DHT22_Check_Response()) {
            // Read 5 bytes: humidity high, humidity low, temp high, temp low, checksum
            Rh_byte1   = DHT22_Read();
            Rh_byte2   = DHT22_Read();
            Temp_byte1 = DHT22_Read();
            Temp_byte2 = DHT22_Read();
            SUM        = DHT22_Read();
            // Combine bytes and convert to float — cast to int16_t handles negative temperature
            temperature = (float)((int16_t)((Temp_byte1 << 8) | Temp_byte2) / 10.0f);
            humidity    = (float)((Rh_byte1 << 8) | Rh_byte2) / 10.0f;
        }

        co2    = (float)Read_ADC(ADC_CHANNEL_5); // MQ-135 raw ADC value
        smoke  = (float)Read_ADC(ADC_CHANNEL_6); // MQ-2 raw ADC value
        light  = BH1750_Read();                   // Light in lux
        motion = (float)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0); // 0 or 1

        /* ── 2. FILL INPUT BUFFER ────────────────────────────────────────── */
        // Load sensor values into buffer in the same order as training data
        input_buffer[0] = co2;
        input_buffer[1] = humidity;
        input_buffer[2] = light;
        input_buffer[3] = motion;
        input_buffer[4] = smoke;
        input_buffer[5] = temperature;

        /* ── 3. CLASSIFICATION ───────────────────────────────────────────── */
        // Run NanoEdge AI inference — fills output_buffer with class probabilities
        // and sets id_class to the index of the highest probability class
        error_code = neai_classification(input_buffer, output_buffer, &id_class);

        /* ── 4. OUTPUT RESULTS ───────────────────────────────────────────── */
        if(error_code == NEAI_OK) {
            // Print sensor values and predicted class name over UART
            printf("%.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %s\r\n",
                    co2, humidity, light, motion, smoke, temperature,
                    neai_get_class_name(id_class)); // retrieves class name string from library
        } else {
            printf("Error: %d\r\n", error_code); // print error code if classification fails
        }

        HAL_Delay(1000); // 1 second sampling interval
    }
}

// Configures MSI oscillator at 16MHz (MSIRANGE_8) as system clock source
// All bus clocks set to same frequency with no division
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2); // Set voltage scaling for 16MHz

    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_MSI; // Use MSI oscillator
    RCC_OscInitStruct.MSIState            = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.MSIClockRange       = RCC_MSIRANGE_8;          // 16MHz
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_NONE;            // PLL not used
    if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    // Configure all bus clock dividers to 1 (no division)
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK3|RCC_CLOCKTYPE_HCLK2|
                                       RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|
                                       RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_MSI; // MSI as system clock
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.AHBCLK2Divider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLK3Divider = RCC_SYSCLK_DIV1;
    if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) Error_Handler();
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

// Disables all interrupts and halts execution — called on any unrecoverable error
void Error_Handler(void)
{
    __disable_irq();
    while(1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif
