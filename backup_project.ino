#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <TM1637Display.h>
#include <Keypad_I2C.h>
#include <Keypad.h>
#include <TimeLib.h>
#include <DS1307RTC.h>
#include <avr/wdt.h>
#include <SoftwareSerial.h>
#include "pitches.h"
#include "my_wdt.h"
#include <avr/sleep.h>


#define DIO 2              // กำหนดหมายเลขพินสำหรับ DIO ของ TM1637
#define CLK 4              // กำหนดหมายเลขพินสำหรับ CLK ของ TM1637
#define RST_PIN 9          // กำหนดหมายเลขพินสำหรับการรีเซ็ตของ RFID
#define SS_PIN 10          // กำหนดหมายเลขพินสำหรับ SS (Slave Select) ของ RFID
#define BUZZER_PIN 8       // กำหนดหมายเลขพินสำหรับ Buzzer
#define LED_PIN 7          // กำหนดหมายเลขพินสำหรับ LED
#define I2CADDR 0x20       // กำหนดที่อยู่ I2C สำหรับ Keypad
#define MAX_CART_ITEMS 10  // กำหนดจำนวนสูงสุดของสินค้าในตะกร้า
#define EEPROM_START_ADDR 0

const byte ROWS = 4;           // จำนวนแถวใน Keypad
const byte COLS = 4;           // จำนวนคอลัมน์ใน Keypad
char hexaKeys[ROWS][COLS] = {  // การแมพปุ่มใน Keypad
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
byte rowPins[ROWS] = { 7, 6, 5, 4 };  // กำหนดหมายเลขพินของแถวใน Keypad
byte colPins[COLS] = { 3, 2, 1, 0 };  // กำหนดหมายเลขพินของคอลัมน์ใน Keypad

const int sw_restock = 5;  // กำหนดหมายเลขพินสำหรับปุ่มรีสต๊อก

Keypad_I2C keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS, I2CADDR);  // สร้างวัตถุ Keypad_I2C
LiquidCrystal_I2C lcd(0x27, 16, 2);                                              // สร้างวัตถุ LCD I2C
MFRC522 rfid(SS_PIN, RST_PIN);                                                   // สร้างวัตถุ RFID
TM1637Display display(CLK, DIO);                                                 // สร้างวัตถุ TM1637Display

tmElements_t tm;                //for DS1307
volatile int count = 0;         // ตัวแปรนับถอยหลัง (volatile เพราะใช้ใน ISR)

int buttonPressed = -1;         // ตัวแปรเก็บปุ่มที่กดไว้
int cartItems[MAX_CART_ITEMS];  // อาร์เรย์เก็บรายการสินค้าในตะกร้า
int cartCount = 0;              // จำนวนสินค้าที่อยู่ในตะกร้า

bool isCardDetected = false;  // ตัวแปรตรวจสอบการตรวจจับบัตร
bool cardEnabled = false;     // ตัวแปรเปิดหรือปิดการใช้บัตร
bool timeout = false;         // ตัวแปรตรวจสอบการหมดเวลา

float cartTotal = 0.0;  // ยอดรวมของสินค้าที่อยู่ในตะกร้า

const char* snacks[] = { "Snack A", "Snack B", "Snack C", "Snack D", "Snack E", "Snack F", "Snack G", "Snack H", "Snack I" };  // ชื่อสินค้า
int prices[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90 };                                                                         // ราคาแต่ละสินค้า
int product_amount[] = { 4, 4, 4, 4, 4, 4, 4, 4, 4 };                                                                          // จำนวนสินค้าในสต๊อก

const unsigned long DEBOUNCE_DELAY = 50;  // ระยะเวลา debounce (มิลลิวินาที)
unsigned long lastDebounceTime = 0;       // เวลาที่ปุ่มสุดท้ายได้รับการกด

char lastKey = NO_KEY;  // ปุ่มสุดท้ายที่ถูกกด

String currentDisplay = "";  // ข้อความที่แสดงใน LCD
bool countingDown = false;   // ตัวแปรสถานะสำหรับการนับถอยหลัง

void saveProductAmountsToEEPROM() {
  for (int i = 0; i < 9; i++) {
    EEPROM.put(EEPROM_START_ADDR + i * sizeof(int), product_amount[i]);
  }
}

void loadProductAmountsFromEEPROM() {
  for (int i = 0; i < 9; i++) {
    EEPROM.get(EEPROM_START_ADDR + i * sizeof(int), product_amount[i]);
  }
}

void clearEEPROM() {
  for (int i = 0; i < EEPROM.length(); i++) {
    EEPROM.write(i, 0xFF);
  }
}

void setup() {
  initial();
  loadProductAmountsFromEEPROM();
}

void initial() {
  Serial.begin(9600);                  // เริ่มต้นการสื่อสาร Serial ที่ 9600 bps
  pinMode(sw_restock, INPUT);          // ตั้งพินรีสต๊อกเป็น input
  pinMode(LED_PIN, OUTPUT);            // ตั้งพิน LED เป็น output
  pinMode(BUZZER_PIN, OUTPUT);         // ตั้งพิน Buzzer เป็น output
  lcd.init();                          // เริ่มต้น LCD
  lcd.backlight();                     // เปิดไฟแบ็คไลท์ของ LCD
  lcd.clear();                         // ล้างหน้าจอ LCD
  displayMessage(" Welcome to EDC ");  // แสดงข้อความต้อนรับบน LCD
  lcd.setCursor(0, 1);                 // ตั้งตำแหน่ง cursor เป็น (0,1)
  lcd.print("billing Machine");        // แสดงข้อความ "Vending Machine" บน LCD
  SPI.begin();                         // เริ่มต้นการสื่อสาร SPI
  rfid.PCD_Init();                     // เริ่มต้น RFID
  display.clear();                     // เคลียร์ TM1637 Display
  display.setBrightness(7);            // ตั้งความสว่างของ TM1637 Display
  Wire.begin();                        // เริ่มต้นการสื่อสาร I2C
  keypad.begin();                      // เริ่มต้น Keypad_I2C

  isCardDetected = false;  // รีเซ็ตสถานะการตรวจจับบัตร
  cardEnabled = false;     // รีเซ็ตสถานะการเปิดใช้งานบัตร
  timeout = false;         // รีเซ็ตสถานะ timeout
  cartTotal = 0.0;

  // ตั้งค่า Timer 1 ให้ทำการ interrupt ทุกๆ 100 มิลลิวินาที
  TCCR1A = 0;                          // ตั้งค่า Timer 1 เป็นโหมดปกติ
  TCCR1B = (1 << CS11) | (1 << CS10);  // ตั้งค่า prescaler เป็น 64
  TCNT1 = 3036;                        // ตั้งค่าเริ่มต้นให้ Timer 1 ทำงานทุกๆ 100 มิลลิวินาที
  TIMSK1 = (1 << TOIE1);               // เปิดการ interrupt ของ Timer 1
  sei();                               // เปิด global interrupt
}

void loop() {
  unsigned long currentTime = millis();  // รับเวลาปัจจุบันจาก millis()
  char key = keypad.getKey();            // อ่านค่าจาก Keypad

  if (key != NO_KEY) {                                        // ถ้ามีการกดปุ่ม
    if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {  // ตรวจสอบว่าเวลาผ่านไปนานพอที่จะไม่เป็นสัญญาณผิดพลาด
      if (key != lastKey) {                                   // ถ้าปุ่มที่กดไม่เหมือนกับปุ่มสุดท้าย
        lastDebounceTime = currentTime;                       // อัปเดตเวลาที่ปุ่มกด
        lastKey = key;                                        // อัปเดตปุ่มสุดท้ายที่กด

        if (key >= '1' && key <= '9') {            // ถ้าปุ่มที่กดเป็น '1'-'9'
          int buttonIndex = key - '1';             // แปลง '1'-'9' เป็น 0-8
          buttonPressed = buttonIndex;             // ตั้งค่าปุ่มที่กดเพื่อประมวลผลการทำรายการ
          displaySnackAndPrice(buttonIndex);       // แสดงข้อมูลสินค้าและราคา
          count = 30;
          countingDown = true;                     // ตั้งค่าเป็นโหมดการนับถอยหลัง
        } else if (key == 'A') {                   // ถ้าปุ่มที่กดเป็น 'A'
          int quantity = 1;                        // Function to get quantity input from the user
          addItemToCart(buttonPressed, quantity);  // Call the updated function with quantity
        } else if (key == 'B') {                   // ถ้าปุ่มที่กดเป็น B
          int quantity = getQuantityFromUser();    // Function to get quantity input from the user
          addItemToCart(buttonPressed, quantity);  // Call the updated function with quantity
        } else if (key == 'D') {
          setProductAmount(buttonPressed);
        } else if (key == '*') {  // ถ้าปุ่มที่กดเป็น '*'
          displayCartTotal();     // แสดงยอดรวมของตะกร้า
          countingDown = false;   // หยุดการนับถอยหลัง
          displayCurrentTime();
        } else if (key == '0') {  // ถ้าปุ่มที่กดเป็น '0'
          timeout = false;
          countingDown = false;
          lcd.setCursor(0, 0);                   // ตั้งตำแหน่ง cursor เป็น (0,0)
          displayMessage("  Reset system!   ");  // แสดงข้อความ "Reset system!"
          lcd.setCursor(0, 1);                   // ตั้งตำแหน่ง cursor เป็น (0,1)
          lcd.print("Please try again");         // แสดงข้อความ "Please try again"
          clearEEPROM();                         // รีเซ็ตระบบ
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("   Return to   ");
          lcd.setCursor(0, 1);
          lcd.print("   Main Menu   ");
          setup();
        }
      }
    }
  }

  if (countingDown) {                       // ถ้าอยู่ในโหมดการนับถอยหลัง
    if (count > 0) {                        // ถ้าค่าการนับถอยหลังยังมากกว่า 0
      display.showNumberDec(count, false);  // แสดงค่าการนับถอยหลังบน TM1637
    } else {
      display.showNumberDec(0, false);  // แสดง 0 บน TM1637
      timeout = true;                   // ตั้งสถานะ timeout เป็นจริง
      countingDown = false;             // หยุดการนับถอยหลัง
    }
  } else {
    displayCurrentTime();
  }

  if (digitalRead(sw_restock) == LOW) {  // ถ้าปุ่มรีสต๊อกถูกกด
    timeout = false;
    countingDown = false;
    restockProduct();  // ทำการรีสต๊อกสินค้า
  }

  if (!timeout) {  // ถ้าไม่มีสถานะ timeout
    // readCard();                             // อ่านบัตร
    // if ((isCardDetected && cardEnabled) || (key == '#')) {    // ถ้าค้นพบบัตรและเปิดใช้งาน
    if (key == '#') {                       // ถ้าค้นพบบัตรและเปิดใช้งาน
      count = 0;                            // รีเซ็ตการนับถอยหลัง
      display.showNumberDec(count, false);  // แสดงการนับถอยหลังเป็น 0 บน TM1637
      processTransaction();                 // ประมวลผลการทำรายการ
    }
  } else {
    lcd.setCursor(0, 0);               // ตั้งตำแหน่ง cursor เป็น (0,0)
    displayMessage("  Time out!   ");  // แสดงข้อความ "Time out!"
    lcd.setCursor(0, 1);               // ตั้งตำแหน่ง cursor เป็น (0,1)
    lcd.print("Please try again");     // แสดงข้อความ "Please try again"
    timeout = false;                   // รีเซ็ตสถานะ timeout
    countingDown = false;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("   Return to   ");
    lcd.setCursor(0, 1);
    lcd.print("   Main Menu   ");
    initial();
  }
}

void displayCurrentTime() {
  if (RTC.read(tm)) {
    display.showNumberDecEx(tm.Hour, 0, true, 2, 0);             // แสดงชั่วโมงบน TM1637
    display.showNumberDecEx(tm.Minute, 0b01000000, true, 2, 2);  // แสดงนาทีบน TM1637
  }
}

void displayCartTotal() {
  lcd.clear();               // เคลียร์ LCD
  lcd.setCursor(0, 0);       // ตั้งตำแหน่ง cursor เป็น (0,0)
  lcd.print("Cart Total:");  // แสดงข้อความ "Cart Total:"
  lcd.setCursor(0, 1);       // ตั้งตำแหน่ง cursor เป็น (0,1)
  lcd.print(cartTotal);      // แสดงยอดรวมของตะกร้า
  lcd.print(" Bath");        // แสดงข้อความ " Bath"
  delay(2000);               // แสดงเป็นเวลา 2 วินาที
}

void displaySnackAndPrice(int index) {
  if (product_amount[index] > 0) {     // ถ้าสินค้ายังมีอยู่
    lcd.clear();                       // เคลียร์ LCD
    lcd.setCursor(0, 0);               // ตั้งตำแหน่ง cursor เป็น (0,0)
    displayMessage(snacks[index]);     // แสดงชื่อสินค้าบน LCD
    lcd.print(" (");                   // แสดงข้อความ "("
    lcd.print(product_amount[index]);  // แสดงจำนวนสินค้าคงเหลือ
    lcd.print(" left)");               // แสดงข้อความ " left)"
    lcd.setCursor(0, 1);               // ตั้งตำแหน่ง cursor เป็น (0,1)
    lcd.print("Price: ");              // แสดงข้อความ "Price: "
    lcd.print(prices[index]);          // แสดงราคาสินค้า
    lcd.print(" Bath");                // แสดงข้อความ " Bath"
  } else {
    displayOutOfStock(index);  // ถ้าสินค้าหมด, แสดงข้อความ "OUT OF STOCK"
  }
  wdt_reset();
}

void displayOutOfStock(int index) {
  lcd.clear();                         // เคลียร์ LCD
  lcd.setCursor(0, 0);                 // ตั้งตำแหน่ง cursor เป็น (0,0)
  displayMessage("     ");             // แสดงข้อความที่ว่างเปล่า
  lcd.print(snacks[index]);            // แสดงชื่อสินค้าบน LCD
  lcd.setCursor(0, 1);                 // ตั้งตำแหน่ง cursor เป็น (0,1)
  displayMessage("  OUT OF STOCK  ");  // แสดงข้อความ "OUT OF STOCK"
}

void displayMessage(String message) {
  if (message != currentDisplay) {  // ถ้าข้อความปัจจุบันไม่ตรงกับข้อความที่จะแสดง
    lcd.clear();                    // เคลียร์ LCD
    lcd.print(message);             // แสดงข้อความ
    currentDisplay = message;       // อัปเดตข้อความที่แสดง
  }
}

void readCard() {
  if (!cardEnabled) {  // ถ้าบัตรถูกปิดการใช้งาน
    return;            // ออกจากฟังก์ชัน
  }
  if (!rfid.PICC_IsNewCardPresent()) {  // ถ้าไม่มีการตรวจจับบัตรใหม่
    isCardDetected = false;             // ตั้งค่าสถานะการตรวจจับบัตรเป็น false
    digitalWrite(LED_PIN, LOW);         // ปิด LED
    return;                             // ออกจากฟังก์ชัน
  }
  if (!rfid.PICC_ReadCardSerial()) {  // ถ้าไม่สามารถอ่านข้อมูลบัตร
    return;                           // ออกจากฟังก์ชัน
  }
  isCardDetected = true;   // ตั้งค่าสถานะการตรวจจับบัตรเป็น true
  rfid.PICC_HaltA();       // หยุดการสื่อสารกับบัตร
  rfid.PCD_StopCrypto1();  // หยุดการเข้ารหัสของ PCD
  delay(200);              // รอ 1 วินาที
}

void enableCard() {
  cardEnabled = true;  // เปิดการใช้งานบัตร
}

void updateProduct(int index, int quantity) {
  if (index >= 0 && index < 9 && product_amount[index] > 0) {  // ถ้าดัชนีถูกต้องและสินค้ามีอยู่
    product_amount[index] -= quantity;                         // ลดจำนวนสินค้าลง
    saveProductAmountsToEEPROM();
    if (product_amount[index] == 0) {
      Serial.print(snacks[index]);
      Serial.println(": OUT OF STOCK ");
    } else {
      Serial.print(snacks[index]);
      Serial.print(" remaining: ");
      Serial.println(product_amount[index]);
    }
  }
}

void setProductAmount(int index) {
  // Temporary variable to store the quantity input from the Keypad
  int quantity = 0;
  int multiplier = 1;
  bool inputComplete = false;

  // Display prompt for user to enter quantity
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Restock quantity:");

  // Loop to get quantity from Keypad
  while (!inputComplete) {
    char key = keypad.getKey();  // Read the key pressed on the Keypad

    if (key) {
      if (key >= '0' && key <= '9') {  // If the key pressed is a digit
        int num = key - '0';           // Convert character to integer
        quantity = quantity + num * multiplier;
        multiplier *= 10;

        // Display the current quantity input
        lcd.setCursor(0, 1);
        lcd.print("Qty: ");
        lcd.print(quantity);

      } else if (key == 'A') {  // If the '#' key is pressed, finish input
        inputComplete = true;
      } else if (key == 'C') {  // If '*' is pressed, cancel and reset
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Canceled");
        delay(200);  // Show cancellation for a short period
        return;
      }
    }
  }

  // Check if the quantity is valid
  if (quantity > 0 && index >= 0 && index < 9) {
    // Update product amount
    product_amount[index] = quantity;

    saveProductAmountsToEEPROM();
    // Display update confirmation
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Updated");
    lcd.setCursor(0, 1);
    lcd.print(snacks[index]);
    lcd.print(": ");
    lcd.print(product_amount[index]);
    lcd.print(" left");
    delay(2000);  // Display for 2 seconds
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Invalid input");
    delay(2000);  // Show error message for 2 seconds
  }
}

int getQuantityFromUser() {
  int quantity = 0;
  int multiplier = 1;
  bool inputComplete = false;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("add quantity:");

  while (!inputComplete) {
    char key = keypad.getKey();

    if (key) {
      if (key >= '0' && key <= '9') {
        int num = key - '0';
        quantity = quantity + num * multiplier;
        multiplier *= 10;
        lcd.setCursor(0, 1);
        lcd.print("Qty: ");
        lcd.print(quantity);
      } else if (key == 'A') {
        inputComplete = true;
      } else if (key == 'C') {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Canceled");
        delay(200);
        break;  // แก้ Error add item 0 quantity
      }
    }
  }

  return quantity;  // Return the final quantity
}

void addItemToCart(int index, int quantity) {
  if (cartCount <= MAX_CART_ITEMS && index >= 0 && index < 9) {  // Check if there is enough space in the cart
    if (product_amount[index] >= quantity) {                     // Check if there is enough stock
      cartTotal += prices[index] * quantity;                     // Add the total price of the added items to cartTotal
      String message = "Added " + String(quantity) + " " + snacks[index];
      updateProduct(index, quantity);  // Update stock quantity
      displayMessage(message);         // Show message on LCD
    } else {
      displayMessage("Not enough stock");  // Display message if stock is insufficient
    }
  } else {
    displayMessage("Cart full");  // Display message if the cart is full
  }
}

void processTransaction() {
  digitalWrite(LED_PIN, HIGH);
  lcd.clear();                 // เคลียร์ LCD
  lcd.setCursor(0, 0);         // ตั้งตำแหน่ง cursor เป็น (0,0)
  lcd.print("Processing...");  // แสดงข้อความ "Processing..."
  delay(200);
  digitalWrite(LED_PIN, LOW);
  lcd.clear();              // เคลียร์ LCD
  lcd.setCursor(0, 0);      // ตั้งตำแหน่ง cursor เป็น (0,0)
  lcd.print("Thank you!");  // แสดงข้อความ "Thank you!"
  delay(200);               // รอ 2 วินาที
  calculateTotalIncome(buttonPressed);
  delay(200);
  initial();
}

void calculateTotalIncome(int index) {
  if (cartCount < MAX_CART_ITEMS && index >= 0 && index < 9) {  // ถ้าตะกร้ายังไม่เต็มและดัชนีถูกต้อง
    cartItems[cartCount] = index;                               // เพิ่มรายการสินค้าไปที่ตะกร้า
    cartCount++;                                                // เพิ่มจำนวนสินค้าในตะกร้า
    cartTotal += prices[index];
  }
  return cartTotal;
}

void restockProduct() {
  for (int i = 0; i < 9; i++) {  // ทำซ้ำสำหรับทุกสินค้า
    product_amount[i] = 4;       // รีเซ็ตจำนวนสินค้าคงเหลือเป็น 4
  }
  saveProductAmountsToEEPROM();
  displayMessage(" Restocking... ");    // แสดงข้อความ "Restocking..."
  delay(200);                           // รอ 1 วินาที
  displayMessage("Restock complete");   // แสดงข้อความ "Restock complete"
  Serial.println("Restock complete!");  // แสดงข้อความใน Serial Monitor
  delay(200);                           // รอ 1 วินาที
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("   Return to   ");
  lcd.setCursor(0, 1);
  lcd.print("   Main Menu   ");
  setup();
  return;
}

// void resetSystem() {
//   wdt_enable(WDTO_15MS);  // เปิดใช้งาน Watchdog Timer ด้วยเวลา 15ms
//   while (1) {}            // วนลูปไม่สิ้นสุดเพื่อกระตุ้นการรีเซ็ต Watchdog Timer
// }

ISR(TIMER1_OVF_vect) {
  count--;  // ลดค่า count ลง 1 ทุกครั้งที่ Timer 1 overflow
}

// ISR(TIMER1_COMPA_vect) {
//   if (countStarted && !countFinished) {
//     if (millisRemaining == 0) {
//       countFinished = true;
//       lastBlinkTime = millis();
//     } else {
//       millisRemaining--;
//     }
//   }
// }