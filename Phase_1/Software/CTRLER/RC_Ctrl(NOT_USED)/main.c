#include <msp430.h>
#include <stdint.h>

#define STARTING_SEQ 0b10100101

#define MIDx 128
#define MIDy 128

#define PI 3.141592654f
#define BODY_RADIUS 50  // mm
#define WHEEL_RADIUS 60 // mm
#define WHEEL_LENGTH 2*WHEEL_RADIUS*PI // mm
#define GEAR_RATIO 44   // :1

uint8_t rx_data;
volatile uint8_t rx_flag = 0;

volatile uint32_t COUNTL = 0;
volatile uint32_t COUNTR = 0;

void Move(uint16_t x, uint16_t y);
void Auto_Move(uint16_t distance, uint16_t angle0, uint16_t angle1);
void SPI_send(uint8_t data);
void clear_count(void);

/**
 * main.c
 */
int main(void)
{
    uint16_t x, y;
    uint16_t distance, angle0, angle1;
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	
	/* Configure the Clock
	 * to run it from DCO @16MHz and SMCLK = DCO / 4 */
	BCSCTL1 = CALBC1_16MHZ;
	DCOCTL = CALDCO_16MHZ;
	BCSCTL2 = DIVS_2 + DIVM_0;

	/* Motor DIR */
	P2DIR |= BIT0 | BIT2 | BIT3 | BIT4;

	/* SPI */
	P1DIR &= ~BIT5;
	P1OUT &= ~BIT5;
	P1SEL = BIT1 | BIT2 | BIT4 | BIT5; // P1.1,2,4 (P1.5 as SS)
	P1SEL2= BIT1 | BIT2 | BIT4 | BIT5;
	UCA0CTL1 = UCSWRST;                         // reset the SPI
	UCA0CTL0 |= UCCKPL + UCMSB + UCSYNC;      // 3-pin, 8-bit SPI Slave
	UCA0CTL1 &= ~UCSWRST;
	IE2 |= UCA0RXIE;

	/* GPIO Interrupt for Sensor feedback */
	P1IES |= BIT3 | BIT6;
	P1IFG &= ~BIT3 | ~BIT6;
	P1IE |= BIT3 | BIT6;

	/* PWM output */
	P2DIR |= BIT1 | BIT5; // P2.1 y P2.4 como salida
	P2SEL |= BIT1 | BIT5; // Asociado al Timer_A1

	TA1CCR0 = 1000-1; // Cargamos el periodo PWM
	TA1CCR1 = 100; // PWM duty cycle, 10% CCR1/(CCR0+1) * 100
	TA1CCR2 = 500; // PWM duty cycle, 50% CCR2/(CCR0+1) * 100
	TA1CCTL1 = OUTMOD_7; //Modo7 reset/set
	TA1CCTL2 = OUTMOD_7; //Modo7 reset/set
	TA1CTL = TASSEL_2 + MC_1; // Timer SMCLK Modo UP

	__bis_SR_register(LPM0_bits + GIE); // Bajo consumo LPM0

	/* Master Initial communication check response */
	P1DIR |= BIT7;  // Warning Indicator
	P1OUT |= BIT7;  // Turn on the indicator
	while (!rx_flag);
	SPI_send(0xFF);
	rx_flag = 0;
	P1OUT &= ~BIT7; // Finish the check, turn off the indicator

	while (1){
	    if (P1IN & BIT0){
	        //Auto
	        // Stop moving
	        P2OUT = 0;
	        TA1CCR1 = 0;
	        TA1CCR2 = 0;
	        while (1){
	            if (rx_flag && rx_data == STARTING_SEQ)
	                break;
	        }
	        rx_flag = 0;
	        P1OUT &= ~BIT7;
	        SPI_send(0b100);

	        while (!rx_flag);
	        distance = rx_data;
	        rx_flag == 0;
	        P1OUT &= ~BIT7;
	        SPI_send(0b101);

	        while (!rx_flag);
	        angle0 = rx_data;
	        rx_flag == 0;
	        P1OUT &= ~BIT7;
	        SPI_send(0b110);

	        while (!rx_flag);
	        angle1 = rx_data;
	        rx_flag == 0;
	        P1OUT &= ~BIT7;
	        Auto_Move(distance, angle0, angle1);
	        SPI_send(0b111);
	    }
	    else {
	        //Controller
	        while (1){
	            if (rx_flag && rx_data == STARTING_SEQ)
	            break;
	        }
	        rx_flag = 0;
	        SPI_send(0b000);
	        P1OUT &= ~BIT7;

	        while (!rx_flag);
	        x = rx_data+1;
	        rx_flag = 0;
	        SPI_send(0b001);
	        P1OUT &= ~BIT7;

	        while (!rx_flag);
	        y = rx_data+1;
	        P1OUT &= ~BIT7;
	        rx_flag = 0;
	        Move(x, y);
	        SPI_send(0b010);
	    }
	}

	return 0;
}

void Move(uint16_t x, uint16_t y){
    int temp_speed, tempx;
    int tempL, tempR;
    if (y == MIDy){
        P2OUT = 0;
        temp_speed = 0;
    }
    else if (y > MIDy){ // Forward
        P2OUT = BIT2 | BIT4;
        temp_speed = ((y - MIDy) * 1000) / MIDy;
    }
    else if (y < MIDy){ // Backward
        P2OUT = BIT0 | BIT3;
        temp_speed = ((MIDy - y) * 1000) / MIDy;
    }

    if (x >= MIDx){ // Right
        tempx = ((x - MIDx) * 1000) / MIDx;
        tempL = temp_speed + tempx;
        tempR = temp_speed - tempx;
    }
    else if (x < MIDx){ // Left
        tempx = ((MIDx - x) * 1000) / MIDx;
        tempL = temp_speed - tempx;
        tempR = temp_speed + tempx;
    }

    if (tempL > 1000) tempL = 1000;
    else if (tempL < 0) tempL = 0;
    if (tempR > 1000) tempR = 1000;
    else if (tempR < 0) tempR = 0;

    TA1CCR1 = tempL;
    TA1CCR2 = tempR;
}

void Auto_Move(uint16_t distance, uint16_t angle0, uint16_t angle1){
    //what?
    // Stop moving
    P2OUT = 0;
    TA1CCR1 = 0;
    TA1CCR2 = 0;

    uint32_t angle = angle0 << 7 | (angle1 >> 1);
    uint16_t direction = angle1 & 1;
    double angle_distance = angle * BODY_RADIUS * GEAR_RATIO / WHEEL_LENGTH / 10000.0;
    uint16_t COUNT_MAX = (uint16_t)angle_distance;

    if (direction) P2OUT = BIT2 | BIT3;
    else P2OUT = BIT0 | BIT4;

    clear_count();
    while (COUNTL <= COUNT_MAX){
        TA1CCR1 = 300;
        TA1CCR2 = 300;
    }
    P2OUT = 0;
    TA1CCR1 = 0;
    TA1CCR2 = 0;

    // Move Forward within a specified distance
    angle_distance = distance * GEAR_RATIO / WHEEL_LENGTH;
    COUNT_MAX = (uint16_t)angle_distance;
    P2OUT = BIT2 | BIT4;
    clear_count();
    while (COUNTR <= COUNT_MAX){
        TA1CCR1 = 500;
        TA1CCR2 = 500;
    }
    P2OUT = 0;
    TA1CCR1 = 0;
    TA1CCR2 = 0;
}

void SPI_send(uint8_t data)
{
    while (!(IFG2 & UCA0TXIFG));              // USCI_A0 TX buffer ready?
    UCA0TXBUF = data;                                                              // Sende Wert
}

void clear_count(void){
    COUNTL = 0;
    COUNTR = 0;
}

#pragma vector=PORT1_VECTOR
__interrupt void Sensor_INT_ISR(void){
    if (P1IFG & BIT3){
        COUNTL += 1;
        P1IFG &= ~BIT3;
    }
    else if (P1IFG & BIT6){
        COUNTR += 1;
        P1IFG &= ~BIT6;
    }
}

#pragma vector=USCIAB0RX_VECTOR
__interrupt void SPI_RX_ISR(void){
    while(UCA0STAT & UCBUSY);
    //while (!(IFG2 & UCA0RXIFG));                        // USCI_A0 TX buffer fertig?
    rx_data = UCA0RXBUF;
    P1OUT |= BIT7;
    rx_flag = 1;
}