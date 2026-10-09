
/**********************************************************************
* Author: F. MAILOM
* CPEG222 Project 4, 10/6/26
* NucleoF466ZE CMSIS Generate Audio Tones using DAC
**********************************************************************/
#include "stm32f4xx.h"
#include "ssd.h"
#include <math.h>

#define SINE_SAMPLES 100
#define DAC_MAX_VALUE 4095
#define DAC_MIDPOINT 2048
#define DAC_AMPLITUDE 1800
#define POT_PORT GPIOC
#define POT_PIN  2
#define SWITCH_PORT GPIOC
#define SWITCH_PIN  8
#define DAC_PORT GPIOA
#define DAC_PIN  4
#define AUDIO_SD_PORT GPIOB
#define AUDIO_SD_PIN  6
#define UP_PORT GPIOF
#define UP_PIN  7
#define NOTE_COUNT 25

uint16_t sineTable[SINE_SAMPLES];
volatile uint16_t ADC_VALUE = 0;
volatile uint16_t FREQUENCY = 100;

void GPIO_Init(void);
void ADC_Init(void);
void DAC_Init(void);
void DMA_Init(void);
void SineTable_Init(void);
uint32_t Get_TIM6_Clock(void);
void TIM6_Init(void);
void Update_Frequency(uint16_t frequency);
static uint16_t Read_Pot_Averaged(void);
void Timebase_Init(void);
void Delay_ms(uint32_t ms);
void Play_HappyBirthday(void);

int main(void)
{
    SSD_Init();
    GPIO_Init();
    ADC_Init();
    DAC_Init();
    Timebase_Init();
    SineTable_Init();
    DMA_Init();
    TIM6_Init();

    // Start DMA after the sine table is filled, then start TIM6
    DMA1_Stream5->CR |= DMA_SxCR_EN;
    TIM6->CR1 |= TIM_CR1_CEN;

    // Show the starting frequency once
    SSD_DisplayValue(FREQUENCY);

    while (1)
    {
        // S1 controls the audio amplifier SD pin (1 = on, 0 = shutdown)
        if (GPIOC->IDR & (1U << SWITCH_PIN))
        {
            GPIOB->BSRR = (1U << AUDIO_SD_PIN);
        }
        else
        {
            GPIOB->BSRR = (1U << (AUDIO_SD_PIN + 16));
        }

        // UP button (active low): play Happy Birthday
        if (!(GPIOF->IDR & (1U << UP_PIN)))
        {
            Delay_ms(20);                          // debounce
            if (!(GPIOF->IDR & (1U << UP_PIN)))
            {
                Play_HappyBirthday();
            }
        }

        // Read the pot (averaged) and map it linearly to 100-2000 Hz
        ADC_VALUE = Read_Pot_Averaged();

        uint16_t new_frequency =
            100 + ((uint32_t)ADC_VALUE * 1900U / 4095U);

        // Only update when the change is at least 2 Hz, to avoid jitter
        int diff = (int)new_frequency - (int)FREQUENCY;
        if (diff >= 2 || diff <= -2)
        {
            Update_Frequency(new_frequency);
            SSD_DisplayValue(FREQUENCY);
        }
    }
}

void GPIO_Init(void)
{
    // Enable GPIOA, GPIOB, and GPIOC clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN |
                    RCC_AHB1ENR_GPIOBEN |
                    RCC_AHB1ENR_GPIOCEN;

    // PC2: potentiometer analog input
    GPIOC->MODER &= ~(3U << (POT_PIN * 2));
    GPIOC->MODER |=  (3U << (POT_PIN * 2));

    // No pull-up or pull-down on analog input
    GPIOC->PUPDR &= ~(3U << (POT_PIN * 2));

    // PC8: slide switch S1 input
    GPIOC->MODER &= ~(3U << (SWITCH_PIN * 2));
    GPIOC->PUPDR &= ~(3U << (SWITCH_PIN * 2));

    // PB6: amplifier shutdown output
    GPIOB->MODER &= ~(3U << (AUDIO_SD_PIN * 2));
    GPIOB->MODER |=  (1U << (AUDIO_SD_PIN * 2));

    // Start with amplifier disabled
    GPIOB->BSRR = (1U << (AUDIO_SD_PIN + 16));

    // PA4: DAC analog output
    GPIOA->MODER &= ~(3U << (DAC_PIN * 2));
    GPIOA->MODER |=  (3U << (DAC_PIN * 2));

    GPIOA->PUPDR &= ~(3U << (DAC_PIN * 2));

    // PF7: UP button input (external pull-up, pressed = 0)
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOFEN;
    GPIOF->MODER &= ~(3U << (UP_PIN * 2));
    GPIOF->PUPDR &= ~(3U << (UP_PIN * 2));
}

void ADC_Init(void)
{
    // Enable ADC1 clock
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    // Set ADC prescaler to PCLK2 / 4
    ADC->CCR &= ~(3U << 16);
    ADC->CCR |=  (1U << 16);

    // Select channel 12 (PC2) for the first conversion
    ADC1->SQR1 = 0;              // One conversion
    ADC1->SQR3 = 12;             // First conversion: channel 12

    // Set a longer sampling time for channel 12
    ADC1->SMPR1 &= ~(7U << 6);
    ADC1->SMPR1 |=  (6U << 6);   // 144 ADC cycles

    // Right-aligned 12-bit conversion, continuous mode disabled
    ADC1->CR1 = 0;
    ADC1->CR2 = 0;

    // Enable ADC1
    ADC1->CR2 |= (1U << 0);
}

void DAC_Init(void)
{
    // Enable DAC and TIM6 clocks
    RCC->APB1ENR |= RCC_APB1ENR_DACEN |
                    RCC_APB1ENR_TIM6EN;

    // Disable DAC channel 1 before configuring it
    DAC->CR &= ~DAC_CR_EN1;

    // Clear channel 1 trigger selection and DMA settings
    DAC->CR &= ~((7U << 3) | DAC_CR_DMAEN1);

    // Select TIM6 TRGO as DAC trigger source
    // TSEL1 = 000
    DAC->CR |= DAC_CR_TEN1;

    // Start with midpoint output
    DAC->DHR12R1 = 2048;

    // Enable DAC channel 1
    DAC->CR |= DAC_CR_EN1;
}

void DMA_Init(void)
{
    // Enable DMA1 clock
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;

    // Disable Stream 5 before configuring it
    DMA1_Stream5->CR &= ~DMA_SxCR_EN;

    // Wait until the stream is disabled
    while (DMA1_Stream5->CR & DMA_SxCR_EN) {}

    // Clear Stream 5 interrupt flags
    DMA1->HIFCR = (0x3DU << 6);

    // Peripheral address: DAC channel 1 data register
    DMA1_Stream5->PAR = (uint32_t)&DAC->DHR12R1;

    // Memory address: sine-wave lookup table
    DMA1_Stream5->M0AR = (uint32_t)sineTable;

    // Number of samples to transfer
    DMA1_Stream5->NDTR = SINE_SAMPLES;

    // Configure Stream 5
    DMA1_Stream5->CR = 0;

    // Channel 7: DAC channel 1
    DMA1_Stream5->CR |= (7U << 25);

    // Memory-to-peripheral direction
    DMA1_Stream5->CR |= (1U << 6);

    // Increment memory address after each sample
    DMA1_Stream5->CR |= DMA_SxCR_MINC;

    // Circular mode: repeat the lookup table
    DMA1_Stream5->CR |= DMA_SxCR_CIRC;

    // Peripheral and memory data widths: 16 bits
    DMA1_Stream5->CR |= (1U << 11) |
                        (1U << 13);

    // High DMA priority
    DMA1_Stream5->CR |= (2U << 16);

    // Enable DMA requests from DAC channel 1
    DAC->CR |= DAC_CR_DMAEN1;
}

void SineTable_Init(void)
{
    for (uint16_t i = 0; i < SINE_SAMPLES; i++)
    {
        float angle = (2.0f * 3.14159265f * i) / SINE_SAMPLES;

        float sample = DAC_MIDPOINT + DAC_AMPLITUDE * sinf(angle);
        sineTable[i] = (uint16_t)sample;
    }
}

uint32_t Get_TIM6_Clock(void)
{
    uint32_t ppre1 = (RCC->CFGR >> 10) & 0x7;
    uint32_t apb1_divider = 1;

    if (ppre1 >= 4)
    {
        apb1_divider = 1U << (ppre1 - 3);
    }

    uint32_t pclk1 = SystemCoreClock / apb1_divider;
    uint32_t tim6_clock = pclk1;

    if (apb1_divider != 1)
    {
        tim6_clock = pclk1 * 2;
    }

    return tim6_clock;
}

void TIM6_Init(void)
{
    TIM6->CR1 = TIM_CR1_ARPE;      // buffer ARR until the next update event
    TIM6->CR2 = (2U << 4);         // update event -> TRGO
    TIM6->PSC = 0;
    TIM6->ARR = (Get_TIM6_Clock() / (FREQUENCY * SINE_SAMPLES)) - 1;
    TIM6->EGR = 1;                 // load preloaded ARR once at startup
    TIM6->SR  = 0;
}

void Update_Frequency(uint16_t frequency)
{
    if (frequency < 100)  frequency = 100;
    if (frequency > 2000) frequency = 2000;

    FREQUENCY = frequency;
    TIM6->ARR = (Get_TIM6_Clock() / (FREQUENCY * SINE_SAMPLES)) - 1;
}

void TIM5_IRQHandler(void)
{
    if (TIM5->SR & TIM_SR_UIF)
    {
        TIM5->SR &= ~TIM_SR_UIF;

        SSD_Refresh();
    }
}

static uint16_t Read_Pot_Averaged(void)
{
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++)
    {
        ADC1->CR2 |= ADC_CR2_SWSTART;
        while (!(ADC1->SR & ADC_SR_EOC)) {}
        sum += ADC1->DR;
    }
    return (uint16_t)(sum / 16);
}

// Free-running 32-bit TIM2 counting at 10 kHz (0.1 ms per tick)
void Timebase_Init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    TIM2->PSC = (Get_TIM6_Clock() / 10000U) - 1;
    TIM2->ARR = 0xFFFFFFFF;
    TIM2->EGR = 1;
    TIM2->CR1 |= TIM_CR1_CEN;
}

void Delay_ms(uint32_t ms)
{
    uint32_t start = TIM2->CNT;
    uint32_t ticks = ms * 10U;

    while ((TIM2->CNT - start) < ticks)
    {
    }
}

void Play_HappyBirthday(void)
{
    // Frequencies (Hz) from the note table, rounded to integers
    // G4=392 A4=440 B4=494 C5=523 D5=587 E5=659 F5=698 G5=784
    static const uint16_t melody[NOTE_COUNT] = {
        392, 392, 440, 392, 523, 494,         // Happy birthday to you
        392, 392, 440, 392, 587, 523,         // Happy birthday to you
        392, 392, 784, 659, 523, 494, 440,    // Happy birthday dear Professor Lum
        698, 698, 659, 523, 587, 523          // Happy birthday to you
    };

    // Note lengths in ms
    static const uint16_t duration[NOTE_COUNT] = {
        300, 150, 450, 450, 450, 900,
        300, 150, 450, 450, 450, 900,
        300, 150, 450, 450, 450, 450, 900,
        300, 150, 450, 450, 450, 900
    };

    for (uint8_t i = 0; i < NOTE_COUNT; i++)
    {
        Update_Frequency(melody[i]);
        SSD_DisplayValue(FREQUENCY);      // show the note's frequency

        Delay_ms(duration[i]);

        // Short silent gap so repeated notes are distinct
        GPIOB->BSRR = (1U << (AUDIO_SD_PIN + 16));
        Delay_ms(30);
        if (GPIOC->IDR & (1U << SWITCH_PIN))
        {
            GPIOB->BSRR = (1U << AUDIO_SD_PIN);
        }
    }

    // Wait for the button to be released so it doesn't retrigger
    while (!(GPIOF->IDR & (1U << UP_PIN)))
    {
    }
    Delay_ms(20);
}