#include <8051.h>

// Pin definitions
#define TRIG_PIN   P1_0 // Trigger pin on P1.0
#define ECHO_PIN   P1_1 // Echo pin on P1.1
#define LCD_RS     P1_2
#define LCD_RW     P1_3
#define LCD_EN     P1_4
#define LCD_OUTPUT P2

__code char API_URL[] = "https://flood-monitoring-lilac.vercel.app/api/flood-data?";
__code char code[]    = "pswd";          // Secret code required to access the api
#define SENSOR_DISTANCE 2000             // Distance between River base and Sensor Placement (in centimeters)
#define PHONE_NUMBER_B  "+9779848015700" // For flood alerts

unsigned char sim_init_success = 0;

/* ================== HELPER FUNCTIONS ================== */
unsigned long measure_distance(void)
{
    unsigned long duration = 0;
    unsigned int timer_value;

    while (ECHO_PIN == 0);
    TMOD = (TMOD & 0xF0) | 0x01; // Timer 0, Mode 1 (16-bit)
    TH0  = 0;
    TL0  = 0;
    TR0  = 1;
    while (ECHO_PIN == 1);
    TR0 = 0;

    // Combining TH0 and TL0 to get full 16 bit timer count.
    timer_value = (TH0 << 8) | TL0;

    // We are multiplying by 1085 because 1 machine cycle for 11.0592Mhz crystal is 1.085 µs
    duration = (unsigned long)timer_value * 1085 / 1000; // Approx. µs
    return duration;
}

void delay_us(unsigned int us)
{
    while (us--) {
        __asm nop __endasm;
        __asm nop __endasm;
        __asm nop __endasm;
    }
}

void delay_ms(unsigned int ms)
{
    unsigned int i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 113; j++);
}

void num_to_string(unsigned long num, char *buf)
{
    unsigned char i = 0, j;
    char temp[10];
    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (num > 0) {
        temp[i++] = (num % 10) + '0';
        num /= 10;
    }
    for (j = 0; j < i; j++) {
        buf[j] = temp[i - 1 - j];
    }
    buf[j] = '\0';
}

void calculate_distances(unsigned long duration, unsigned long *height)
{
    unsigned long cm = duration / 58; // Convert duration (µs) to cm using speed of sound (340 m/s)
    // *height          = SENSOR_DISTANCE - cm;
    *height = cm;
}
/*==================================== */

/* ================== SENSOR TRIGGERING CODE ================== */
void trigger_sensor(void)
{
    TRIG_PIN = 0;
    delay_us(2); // 2 ms delay
    TRIG_PIN = 1;
    delay_us(10); // 10 delay
    TRIG_PIN = 0;
}
/*==================================== */

/* ================== UART SERIAL COMMUNICATION MODULE ================== */
void init_serial(void)
{
    SCON = 0x50; // Mode 1, 8-bit UART, enable receiver
    TMOD = 0x20; // Timer 1, Mode 2 (auto-reload)
    TH1  = 0xFD; // Reload value for 9600 baud at 11.0592 MHz
    TR1  = 1;    // Start Timer 1
    TI   = 0;    // Clear transmit interrupt flag
}

void send_char(unsigned char c)
{
    SBUF = c;
    while (!TI);
    TI = 0;
}

void send_string(char *str)
{
    while (*str) {
        send_char(*str++);
    }
}
/*==================================== */

/* ================== LCD MODULE ================== */
void lcd_command(unsigned char cmd)
{
    LCD_RS     = 0; // Command mode
    LCD_RW     = 0; // Write mode
    LCD_OUTPUT = cmd;
    LCD_EN     = 1; // Enable pulse
    delay_us(10);
    LCD_EN = 0;
    delay_ms(2); // Wait for command to execute
}

void lcd_data(unsigned char dat)
{
    LCD_RS     = 1; // Data mode
    LCD_RW     = 0; // Write mode
    LCD_OUTPUT = dat;
    LCD_EN     = 1; // Enable pulse
    delay_us(10);
    LCD_EN = 0;
    delay_us(100); // Wait for data to be written
}

void lcd_init(void)
{
    delay_ms(20);      // LCD power-up delay
    lcd_command(0x38); // 8-bit mode, 2 lines, 5x7 font
    lcd_command(0x0C); // Display on, cursor off
    lcd_command(0x06); // Increment cursor, no shift
    lcd_command(0x01); // Clear display
    delay_ms(2);
}

void lcd_string(char *str)
{
    while (*str) {
        lcd_data(*str++);
    }
}

void lcd_display_height(unsigned long height)
{
    char height_str[10];

    lcd_command(0x80); // Move cursor to first line
    lcd_string("Height: ");

    num_to_string(height, height_str); // Reuse your existing function
    lcd_string(height_str);
    lcd_string(" cm");
}
/*==================================== */

/* ================== SIM MODULE CODE ================== */
void read_response(char *buf, int buf_size, unsigned int timeout_ms)
{
    int i = 0;
    for (unsigned int j = 0; j < timeout_ms / 10; j++) { // Timeout in ms
        delay_ms(10);
        while (RI && i < buf_size - 1) {
            buf[i++] = SBUF;
            RI       = 0;
        }
    }
    buf[i] = '\0';
}

int send_at_command(char *command)
{
    send_string(command);
    char response[10];
    read_response(response, 10, 1000); // 1s timeout
    // Clear LCD
    // Check for "OK"
    // for (int k = 0; k < 9; k++) {
    //     if (response[k] == 'O' && response[k + 1] == 'K') {
    //         return 1; // Success
    //     }
    // }
    // return 0; // Failure, no SMS sent
    return 1;
}

void send_sms(char *number, char *message)
{
    // Set SMS text mode
    if (!send_at_command("AT+CMGF=1\r")) return;
    delay_ms(500);
    // Set recipient
    send_string("AT+CMGS=\"");
    send_string(number);
    send_string("\"\r");
    delay_ms(500);
    // Send message
    send_string(message);
    // Send Ctrl+Z
    SBUF = 0x1A;
    while (!TI);
    TI = 0;
    delay_ms(2000);
}

void init_sim(void)
{

    // sim_init_success = send_at_command("AT\r") &&
    //                    send_at_command("ATE0\r") &&
    //                    send_at_command("AT+CPIN?\r") &&
    //                    send_at_command("AT+CSQ\r") &&
    //                    send_at_command("AT+CGATT=1\r") &&
    //                    send_at_command("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"\r") &&
    //                    send_at_command("AT+SAPBR=3,1,\"APN\",\"ntnet\"\r") &&
    //                    send_at_command("AT+SAPBR=1,1\r") &&
    //                    send_at_command("AT+SAPBR=2,1\r") &&
    //                    send_at_command("AT+HTTPINIT\r") &&
    //                    send_at_command("AT+HTTPSSL=1\r") &&
    //                    send_at_command("AT+HTTPPARA=\"CID\",1\r");
    send_at_command("AT\r");
    send_at_command("ATE0\r");
    send_at_command("AT+CPIN?\r");
    send_at_command("AT+CSQ\r");
    send_at_command("AT+CGATT=1\r");
    send_at_command("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"\r");
    send_at_command("AT+SAPBR=3,1,\"APN\",\"ntnet\"\r");
    send_at_command("AT+SAPBR=1,1\r");
    send_at_command("AT+SAPBR=2,1\r");
    send_at_command("AT+HTTPINIT\r");
    send_at_command("AT+HTTPSSL=1\r");
    send_at_command("AT+HTTPPARA=\"CID\",1\r");
}

void send_http_get(unsigned long duration)
{
    if (!sim_init_success) return;
    char duration_str[10];
    num_to_string(duration, duration_str);
    // Set HTTP URL
    send_string("AT+HTTPPARA=\"URL\",\"");
    send_string(API_URL);
    send_string("code=");
    send_string(code);
    send_string("&duration=");
    send_string(duration_str);
    send_string("\"\r");
    // Perform GET request
    if (!send_at_command("AT+HTTPACTION=0\r")) return;
}

void send_flood_alert_msg()
{
    send_sms(PHONE_NUMBER_B, "FLOOD ALERT! Water level has exceeded 1000cm!");
}
/*==================================== */

void main(void)
{
    unsigned long duration, height;
    init_serial();
    lcd_init();
    static int alert_sent = 0;
    EA                    = 0; // Disable interrupts
    init_sim();
    while (1) {
        trigger_sensor();
        duration = measure_distance();
        calculate_distances(duration, &height);
        lcd_display_height(height);
        if (height > 1000 && !alert_sent) {
            send_flood_alert_msg();
            alert_sent = 1;
        } else if (height <= 1000) {
            alert_sent = 0;
        }
        if (sim_init_success) {
            send_http_get(duration);
        }
        delay_ms(1000);
    }
}