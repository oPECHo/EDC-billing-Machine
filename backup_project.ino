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

#define DIO 2              // กำหนดหมายเลขพินสำหรับ DIO ของ TM1637
#define CLK 3              // กำหนดหมายเลขพินสำหรับ CLK ของ TM1637
#define RST_PIN 9          // กำหนดหมายเลขพินสำหรับการรีเซ็ตของ RFID
#define SS_PIN 10          // กำหนดหมายเลขพินสำหรับ SS (Slave Select) ของ RFID
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
byte rowPins[ROWS] = { 7, 6, 5, 4 };                                             // กำหนดหมายเลขพินของแถวใน Keypad
byte colPins[COLS] = { 3, 2, 1, 0 };                                             // กำหนดหมายเลขพินของคอลัมน์ใน Keypad
Keypad_I2C keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS, I2CADDR);  // สร้างวัตถุ Keypad_I2C
LiquidCrystal_I2C lcd(0x27, 16, 2);                                              // สร้างวัตถุ LCD I2C

MFRC522 rfid(SS_PIN, RST_PIN);  // สร้างวัตถุ RFID

// Authentication key
MFRC522::MIFARE_Key key;
const byte block = 4;
const byte trailerBlock = 7;

TM1637Display display(CLK, DIO);  // สร้างวัตถุ TM1637Display
const char *monthName[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

struct CartItem {
  int index;     // ตำแหน่ง index ของสินค้า
  int quantity;  // จำนวนสินค้าที่สั่ง
};

tmElements_t tm;  //for DS1307

bool timeout = false;         // ตัวแปรตรวจสอบการหมดเวลา
bool dotOn = true;            // สถานะปัจจุบันของจุดสองจุด
bool countingDown = false;    // ตัวแปรสถานะสำหรับการนับถอยหลัง
bool getTime(const char *str) {
  int Hour, Min, Sec;

  if (sscanf(str, "%d:%d:%d", &Hour, &Min, &Sec) != 3) return false;
  tm.Hour = Hour;
  tm.Minute = Min;
  tm.Second = Sec;
  return true;
}

bool getDate(const char *str) {
  char Month[12];
  int Day, Year;
  uint8_t monthIndex;

  if (sscanf(str, "%s %d %d", Month, &Day, &Year) != 3) return false;
  for (monthIndex = 0; monthIndex < 12; monthIndex++) {
    if (strcmp(Month, monthName[monthIndex]) == 0) break;
  }
  if (monthIndex >= 12) return false;
  tm.Day = Day;
  tm.Month = monthIndex + 1;
  tm.Year = CalendarYrToTm(Year);
  return true;
}

float cartTotal = 0.0;  // ยอดรวมของสินค้าที่อยู่ในตะกร้า
float product_sales[9] = { 0.0 };
float totalIncome = 0.0;

char lastKey = NO_KEY;                                                                                                         // ปุ่มสุดท้ายที่ถูกกด
const char *snacks[] = { "Snack A", "Snack B", "Snack C", "Snack D", "Snack E", "Snack F", "Snack G", "Snack H", "Snack I" };  // ชื่อสินค้า

int prices[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90 };  // ราคาแต่ละสินค้า
int product_amount[] = { 4, 4, 4, 4, 4, 4, 4, 4, 4 };   // จำนวนสินค้าในสต๊อก
int cartCount = 0;                                      // จำนวนสินค้าที่อยู่ในตะกร้า
int buttonPressed = -1;                                 // ตัวแปรเก็บปุ่มที่กดไว้
volatile int count = 0;                                 // ตัวแปรนับถอยหลัง (volatile เพราะใช้ใน ISR)
const int sw_restock = 5;                               // กำหนดหมายเลขพินสำหรับปุ่มรีสต๊อก

const unsigned long DEBOUNCE_DELAY = 50;  // ระยะเวลา debounce (มิลลิวินาที)
unsigned long previousMillis = 0;         // ใช้เก็บเวลาที่ผ่านมา
unsigned long lastDebounceTime = 0;       // เวลาที่ปุ่มสุดท้ายได้รับการกด

String currentDisplay = "";          // ข้อความที่แสดงใน LCD
CartItem cartItems[MAX_CART_ITEMS];  // อาร์เรย์เก็บรายการสินค้าในตะกร้า

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
  Serial.begin(38400);
  while (!Serial)
    ;  // wait for Arduino Serial Monitor
  delay(200);

  // Load product amounts from EEPROM
  loadProductAmountsFromEEPROM();

  // Call initial to configure hardware and DS1307
  initial();
}

void initial() {
  pinMode(sw_restock, INPUT);   // Set pin for restocking as input

  lcd.init();                          // Initialize LCD
  lcd.backlight();                     // Turn on LCD backlight
  lcd.clear();                         // Clear LCD display
  displayMessage(" Welcome to EDC ");  // Display welcome message on LCD
  lcd.setCursor(0, 1);                 // Set cursor position to (0,1)
  lcd.print("billing Machine");        // Display "Vending Machine" on LCD

  SPI.begin();      // Initialize SPI communication
  rfid.PCD_Init();  // Initialize RFID
  rfid.PCD_DumpVersionToSerial();

  // Prepare the key
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  Serial.println(F("====================================================="));
  Serial.println(F("SET CARD BALANCE"));
  Serial.println(F("====================================================="));
  Serial.print(F("Using key:"));
  dump_byte_array(key.keyByte, MFRC522::MF_KEY_SIZE);

  display.clear();           // Clear TM1637 Display
  display.setBrightness(7);  // Set brightness of TM1637 Display
  Wire.begin();              // Initialize I2C communication
  keypad.begin();            // Initialize Keypad_I2C

  timeout = false;  // Reset timeout status
  cartTotal = 0.0;

  // Reset EEPROM to default values
  // clearEEPROM();

  // Set DS1307 to compile time and date
  bool parse = false;
  bool config = false;

  if (getDate(__DATE__) && getTime(__TIME__)) {
    parse = true;
    if (RTC.write(tm)) {
      config = true;
    }
  }

  if (parse && config) {
    Serial.print("DS1307 configured Time=");
    Serial.print(__TIME__);
    Serial.print(", Date=");
    Serial.println(__DATE__);
  } else if (parse) {
    Serial.println("DS1307 Communication Error :-{");
    Serial.println("Please check your circuitry");
  } else {
    Serial.print("Could not parse info from the compiler, Time=\"");
    Serial.print(__TIME__);
    Serial.print("\", Date=\"");
    Serial.print(__DATE__);
    Serial.println("\"");
  }

  // Timer initialization for countdown
  TCCR1A = 0;             // Set Timer 1 to normal mode
  TCCR1B = (1 << CS12);   // Set prescaler to 64
  TCNT1 = 3036;           // Set Timer 1 to overflow every 100 ms
  TIMSK1 = (1 << TOIE1);  // Enable Timer 1 overflow interrupt
  sei();                  // Enable global interrupt
}


void loop() {
  unsigned long currentTime = millis();  // รับเวลาปัจจุบันจาก millis()
  char key = keypad.getKey();            // อ่านค่าจาก Keypad

  if (key != NO_KEY) {                                        // ถ้ามีการกดปุ่ม
    if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {  // ตรวจสอบว่าเวลาผ่านไปนานพอที่จะไม่เป็นสัญญาณผิดพลาด
      if (key != lastKey) {                                   // ถ้าปุ่มที่กดไม่เหมือนกับปุ่มสุดท้าย
        lastDebounceTime = currentTime;                       // อัปเดตเวลาที่ปุ่มกด
        lastKey = key;                                        // อัปเดตปุ่มสุดท้ายที่กด

        if (key >= '1' && key <= '9') {       // ถ้าปุ่มที่กดเป็น '1'-'9'
          int buttonIndex = key - '1';        // แปลง '1'-'9' เป็น 0-8
          buttonPressed = buttonIndex;        // ตั้งค่าปุ่มที่กดเพื่อประมวลผลการทำรายการ
          displaySnackAndPrice(buttonIndex);  // แสดงข้อมูลสินค้าและราคา
          // Serial.println(key);
          count = 30;
          countingDown = true;                     // ตั้งค่าเป็นโหมดการนับถอยหลัง
        } else if (key == 'A') {                   // ถ้าปุ่มที่กดเป็น 'A'
          int quantity = 1;                        // Function to get quantity input from the user
          addItemToCart(buttonPressed, quantity);  // Call the updated function with quantity
        } else if (key == 'B') {                   // ถ้าปุ่มที่กดเป็น B
          int quantity = getQuantityFromUser();
          addItemToCart(buttonPressed, quantity);  // Call the updated function with quantity
        } else if (key == 'C') {
          CancelProductsInCart();
        } else if (key == 'D') {
          setProductAmount(buttonPressed);
        } else if (key == '*') {  // ถ้าปุ่มที่กดเป็น '*'
          displayCartTotal();     // แสดงยอดรวมของตะกร้า
          countingDown = false;   // หยุดการนับถอยหลัง
          displayCurrentTime();
        } else if (key == '0') {  // ถ้าปุ่มที่กดเป็น '0'
          timeout = false;
          countingDown = false;
          displayMessage("  Reset system!   ");  // แสดงข้อความ "Reset system!"
          lcd.setCursor(0, 1);                   // ตั้งตำแหน่ง cursor เป็น (0,1)
          lcd.print("Please try again");         // แสดงข้อความ "Please try again"
          clearEEPROM();                         // รีเซ็ตระบบ
          displayMessage("  Return to  ");
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

  if (!timeout) {                                                        // ถ้าไม่มีสถานะ timeout
                                                                         // readCard();                                           // อ่านบัตร
    if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {  // ถ้าค้นพบบัตรและเปิดใช้งาน
      return;
    } else {
      count = 0;                            // รีเซ็ตการนับถอยหลัง
      display.showNumberDec(count, false);  // แสดงการนับถอยหลังเป็น 0 บน TM1637
      Serial.println(F("A new card has appeared"));
      totalIncome = TotlatIncome();  // รับค่าที่คืนกลับ
      processCard(totalIncome);      // ส่ง TotalIncome ไปยัง processCard
      delay(1500);
      processTransaction();
      totalIncome = 0.00;
    }
  } else {
    displayMessage("  Time out!   ");  // แสดงข้อความ "Time out!"
    lcd.setCursor(0, 1);               // ตั้งตำแหน่ง cursor เป็น (0,1)
    lcd.print("Please try again");     // แสดงข้อความ "Please try again"
    timeout = false;                   // รีเซ็ตสถานะ timeout
    countingDown = false;
    displayMessage("   Return to   ");
    lcd.setCursor(0, 1);
    lcd.print("   Main Menu   ");
    initial();
  }
}

void displayCurrentTime() {
  unsigned long currentMillis = millis();  // อ่านเวลาปัจจุบัน

  // ตรวจสอบว่าเวลาที่ผ่านมาเกินช่วงเวลาที่กำหนดหรือไม่
  if (currentMillis - previousMillis >= 500) {
    previousMillis = currentMillis;  // อัพเดตเวลาที่ผ่านมา
    dotOn = !dotOn;                  // สลับสถานะของจุดสองจุด
  }

  if (RTC.read(tm)) {
    if (dotOn) {
      display.showNumberDecEx(tm.Hour, 0b01000000, true, 2, 0);
      display.showNumberDecEx(tm.Minute, 0b01000000, true, 2, 2);
    } else {
      display.showNumberDecEx(tm.Hour, 0b00000000, true, 2, 0);
      display.showNumberDecEx(tm.Minute, 0b00000000, true, 2, 2);  // ปิดจุดสองจุด
    }
  }
}


void displayCartTotal() {
  displayMessage("Cart Total:");  // แสดงข้อความ "Cart Total:"
  lcd.setCursor(0, 1);            // ตั้งตำแหน่ง cursor เป็น (0,1)
  lcd.print(cartTotal);           // แสดงยอดรวมของตะกร้า
  lcd.print(" Bath");             // แสดงข้อความ " Bath"
  delay(2000);                    // แสดงเป็นเวลา 2 วินาที
}

void displaySnackAndPrice(int index) {
  if (product_amount[index] > 0) {     // ถ้าสินค้ายังมีอยู่
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
}

void displayOutOfStock(int index) {
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

void processCard(float totalIncome) {
  if (block != 4) {
    Serial.print(F("Invalid block: "));
    Serial.println(block);
    halt();
    return;
  }

  Serial.print(F("Card UID: "));
  dump_byte_array(rfid.uid.uidByte, rfid.uid.size);

  if (!authenticateCard()) return;

  int currentAmount = readBalance();
  if (currentAmount < 0) return;

  int newAmount = currentAmount - totalIncome;
  if (newAmount < 0) {
    Serial.println(F("Amount after decrease is negative. Setting to 0."));
    newAmount = 0;
  }

  writeBalance(newAmount);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Update amount: ");
  lcd.setCursor(0, 1);
  lcd.print(newAmount);
  lcd.print(" Bath"); 
  confirmBalance();
  halt();
}

bool authenticateCard() {
  MFRC522::StatusCode status;
  Serial.println(F("Authenticating using key A"));
  status = (MFRC522::StatusCode)rfid.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(rfid.uid));

  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("PCD_Authenticate() failed: "));
    Serial.println(rfid.GetStatusCodeName(status));
    return false;
  }
  return true;
}

int readBalance() {
  byte buffer[18];
  byte size = sizeof(buffer);
  MFRC522::StatusCode status;

  Serial.print(F("Reading balance from card (block "));
  Serial.print(block);
  Serial.println(F(")"));
  status = (MFRC522::StatusCode)rfid.MIFARE_Read(block, buffer, &size);

  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("MIFARE_Read() failed: "));
    Serial.println(rfid.GetStatusCodeName(status));
    return -1;  // Error indicator
  }

  int currentAmount = (buffer[0] << 8) | buffer[1];
  Serial.print(F("Current amount: "));
  Serial.println(currentAmount);
  return currentAmount;
}

void writeBalance(int newAmount) {
  byte balanceData[16] = { 0 };
  balanceData[0] = (newAmount >> 8) & 0xFF;  // High byte
  balanceData[1] = newAmount & 0xFF;         // Low byte

  Serial.print(F("Writing updated amount "));
  Serial.print(newAmount);
  Serial.print(F(" to card (block "));
  Serial.print(block);
  Serial.println(F(")"));

  MFRC522::StatusCode status = (MFRC522::StatusCode)rfid.MIFARE_Write(block, balanceData, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("MIFARE_Write() failed: "));
    Serial.println(rfid.GetStatusCodeName(status));
  } else {
    Serial.println(F("Write finished"));
  }
}

void confirmBalance() {
  byte buffer[18];
  byte size = sizeof(buffer);
  MFRC522::StatusCode status;

  Serial.print(F("Reading updated amount from card (block "));
  Serial.print(block);
  Serial.println(F(")"));
  status = (MFRC522::StatusCode)rfid.MIFARE_Read(block, buffer, &size);

  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("MIFARE_Read() failed: "));
    Serial.println(rfid.GetStatusCodeName(status));
  } else {
    int updatedAmount = (buffer[0] << 8) | buffer[1];
    Serial.print(F("Updated amount read: "));
    Serial.println(updatedAmount);
  }
}

void halt() {
  Serial.println(F("Halting loop"));
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void dump_byte_array(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
  Serial.println();
}

void dump_byte_array_decimal(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    if (buffer[i] != 0x00) {
      Serial.print(buffer[i], DEC);
    }
  }
  Serial.println(F(""));
}

void updateProduct(int index, int quantity) {
  if (index >= 0 && index < 9 && product_amount[index] > 0) {  // ถ้าดัชนีถูกต้องและสินค้ามีอยู่
    product_amount[index] -= quantity;                         // ลดจำนวนสินค้าลง
    saveProductAmountsToEEPROM();
  }
}

void addItemToCart(int index, int quantity) {
  if (cartCount < MAX_CART_ITEMS && index >= 0 && index < 9) {  // ตรวจสอบว่าตะกร้าพร้อมใช้งาน
    if (product_amount[index] >= quantity) {                    // ตรวจสอบจำนวนสินค้าในสต๊อก
      cartItems[cartCount].index = index;                       // บันทึก index สินค้า
      cartItems[cartCount].quantity = quantity;                 // บันทึกจำนวนสินค้าที่สั่ง
      cartTotal += prices[index] * quantity;                    // เพิ่มราคาสินค้าลงในยอดรวม
      updateProduct(index, quantity);                           // อัปเดตจำนวนสินค้าในสต๊อก
      cartCount++;                                              // เพิ่มจำนวนสินค้าต่อไปในตะกร้า
      displayMessage("Item added");                             // แสดงข้อความการเพิ่มสินค้า
    } else {
      displayMessage("Not enough stock");  // แสดงข้อความเมื่อสต๊อกไม่เพียงพอ
    }
  } else {
    displayMessage("Cart full");  // แสดงข้อความเมื่อเต็มตะกร้า
  }
}


void CancelProductsInCart() {
  for (int i = 0; i < cartCount; i++) {
    int index = cartItems[i].index;
    int quantity = cartItems[i].quantity;
    product_amount[index] += quantity;  // เพิ่มจำนวนสินค้าคืน
    Serial.print(snacks[index]);
    Serial.print(", Remaining Stock: ");
    Serial.println(product_amount[index]);
  }

  cartCount = 0;    // เคลียร์ตะกร้า
  cartTotal = 0.0;  // เคลียร์ยอดรวม
  saveProductAmountsToEEPROM();
  displayMessage("Products");
  lcd.setCursor(0, 1);
  lcd.print("Returned");
}


void setProductAmount(int index) {
  // Temporary variable to store the quantity input from the Keypad
  int quantity = 0;
  int multiplier = 1;
  bool inputComplete = false;

  // Display prompt for user to enter quantity
  displayMessage("Restock quantity:");

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
        displayMessage("Canceled");
        delay(1000);  // Show cancellation for a short period
        return;
      }
    }
  }

  // Check if the quantity is valid
  if (quantity > 0 && index >= 0 && index < 9) {
    // Update product amount
    product_amount[index] += quantity;

    saveProductAmountsToEEPROM();
    // Display update confirmation
    displayMessage("Updated");
    lcd.setCursor(0, 1);
    lcd.print(snacks[index]);
    lcd.print(": ");
    lcd.print(product_amount[index]);
    lcd.print(" left");
    delay(2000);  // Display for 2 seconds
  } else {
    displayMessage("Invalid input");
    delay(2000);  // Show error message for 2 seconds
  }
}

int getQuantityFromUser() {
  int quantity = 0;
  int multiplier = 1;
  bool inputComplete = false;

  displayMessage("add quantity");

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
        displayMessage("Canceled");
        delay(1000);
      } else {
        displayMessage("Invalid input");
        delay(1000);  // Show error message for 2 seconds
      }
    }
  }

  return quantity;  // Return the final quantity
}
float TotlatIncome() {
  for (int i = 0; i < cartCount; i++) {
    int index = cartItems[i].index;
    int quantity = cartItems[i].quantity;
    float itemTotal = prices[index] * quantity;
    Serial.print(snacks[index]);
    Serial.print(": ");
    Serial.print(quantity);
    Serial.print(" units, ");
    Serial.print(itemTotal);
    Serial.println(" Bath");
    totalIncome += itemTotal;  // Accumulate total income
  }
  return totalIncome;
}

void processTransaction() {
  displayMessage("Processing...");
  delay(1000);  // Wait for 1 second
  displayMessage("Thank you!");
  delay(1000);  // Wait for 1 second

  // Calculate total income and print product details
  totalIncome = 0.0;
  Serial.println("Product Details:");
  for (int i = 0; i < cartCount; i++) {
    int index = cartItems[i].index;
    int quantity = cartItems[i].quantity;
    float itemTotal = prices[index] * quantity;
    Serial.print(snacks[index]);
    Serial.print(": ");
    Serial.print(quantity);
    Serial.print(" units, ");
    Serial.print(itemTotal);
    Serial.println(" Bath");
    totalIncome += itemTotal;  // Accumulate total income
  }
  Serial.print("Total income: ");
  Serial.print(totalIncome);
  Serial.println(" Bath");
  // Print remaining stock for each product
  Serial.println("Detail Product:");
  for (int i = 0; i < 9; i++) {
    if (product_amount[i] == 0) {
      Serial.print(snacks[i]);
      Serial.println(": OUT OF STOCK ");
    } else {
      Serial.print(snacks[i]);
      Serial.print(": remaining: ");
      Serial.println(product_amount[i]);
    }
  }

  // Clear the cart and reset totals
  cartCount = 0;
  cartTotal = 0.0;

  // Save updated product quantities to EEPROM
  saveProductAmountsToEEPROM();

  // Display completion message on LCD
  displayMessage("Transaction");
  lcd.setCursor(0, 1);
  lcd.print("Complete");
  delay(1000);  // Wait for 1 second

  // Return to the main menu
  displayMessage("   Return to   ");
  lcd.setCursor(0, 1);
  lcd.print("   Main Menu   ");
  delay(1000);  // Wait for 1 second

  setup();  // Reinitialize or reset as needed
  return totalIncome;
}

void restockProduct() {
  for (int i = 0; i < 9; i++) {  // ทำซ้ำสำหรับทุกสินค้า
    product_amount[i] = 4;       // รีเซ็ตจำนวนสินค้าคงเหลือเป็น 4
  }
  saveProductAmountsToEEPROM();
  displayMessage(" Restocking... ");    // แสดงข้อความ "Restocking..."
  delay(1000);                          // รอ 1 วินาที
  displayMessage("Restock complete");   // แสดงข้อความ "Restock complete"
  Serial.println("Restock complete!");  // แสดงข้อความใน Serial Monitor
  delay(1000);                          // รอ 1 วินาที
  displayMessage("   Return to   ");
  lcd.setCursor(0, 1);
  lcd.print("   Main Menu   ");
  setup();
  return;
}

ISR(TIMER1_OVF_vect) {
  count--;  // ลดค่า count ลง 1 ทุกครั้งที่ Timer 1 overflow
}