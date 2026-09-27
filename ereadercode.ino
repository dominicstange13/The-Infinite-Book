#include <GxEPD2_BW.h>
#include <SPI.h>
#include <SD.h>

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
    GxEPD2_420_GDEY042T81(
        PIN_EPD_CS,
        PIN_EPD_DC,
        PIN_EPD_RESET,
        PIN_EPD_BUSY
    )
);

const int BAT_PIN = A0;
const int R_BUTTON = 6;
const int L_BUTTON = 5;
const int SELECT_BUTTON = 9;
const int SD_CS = 25;
const int SD_DET = 24;
int lastRbuttonState = HIGH;
int lastLbuttonState = HIGH;
int lastSelectButtonState = HIGH;
int currentRbuttonState = LOW;
int currentLbuttonState = LOW;
int currentSelectButtonState = LOW;
bool movingCursor = false;
bool inMenu = true;
int cursorIndex = 0;
uint32_t pageStart = 0;
bool pageNeedsUpdate = false;
int refreshCounter = 0;
unsigned long lastButtonTime = 0;
const unsigned long debounceTime = 50;

const float VOLTAGES[] = {
  4.20, 4.15, 4.11, 4.08, 4.02, 3.98, 3.95, 3.91, 3.87, 3.85,
  3.84, 3.82, 3.8, 3.79, 3.77, 3.75, 3.73, 3.71, 3.69, 3.61, 3.27
};

const int PERCENTAGES[] = {
  100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 5, 0
};

int batValues = 21;

const int barUpdTime = 6000;
unsigned long previousMillis = 0;

const unsigned long sleepTime = 300000;
unsigned long lastActivityTime = 0;
bool displaySleeping = false;

File books[100];
int bookIndex = 0;
int amountOfBooks = 0;

uint32_t pagePositions[5000];
int currentPage = 0;
uint32_t bookBookmarks[100];
int bookCurrentPages[100];

float readBattery() {
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(A0);
    delay(2);
  }
  float raw = sum / 20.0;
  float voltage = (raw * 3.3 / 1023.0) * 2.0; voltage = (voltage * (4.20/4.17));
  return voltage;
}

void loadBooks() {
  File root = SD.open("/");
  int index = 0;

  while (true) {
    File entry = root.openNextFile();

    if (!entry) {
      break;
    }

    if (!entry.isDirectory() && index < 100) {
      books[index] = SD.open(entry.name(), FILE_READ);
      Serial.print("Loaded: ");
      Serial.println(books[index].name());
      index++;
    }

    entry.close();
  }

  root.close();
  amountOfBooks = index;

  Serial.print("Total books: ");
  Serial.println(amountOfBooks);
}

void sleepDisplay() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());
  display.hibernate();
  displaySleeping = true;
  Serial.println("Display hibernating");
}

void wakeDisplay() {
  display.init();
  display.setRotation(0);
  display.setTextSize(1);
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();
  displaySleeping = false;
  Serial.println("Display waking");
  if (inMenu) {
    drawMenu();
  } else {
    drawPage();
  }
}

void updateBar() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= barUpdTime) {

    float batteryVoltage = readBattery();
    float batteryPercentage = readPercentage(batteryVoltage);
    batteryPercentage = constrain(batteryPercentage, 0.0, 100.0);
    if (movingCursor == false) {
      previousMillis = currentMillis;
      display.setPartialWindow(0, 255, 400, 45);
      display.firstPage();
      do {
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(355, 275);
        display.print(int(round(batteryPercentage)));
        display.println("%");
        display.drawRect(347, 267, 39, 26, GxEPD_BLACK);
        display.drawRect(385, 271, 7, 18, GxEPD_BLACK);
        display.drawLine(0, 260, 400, 260, GxEPD_BLACK);
      } while (display.nextPage());
    }
  }
}

void drawMenu() {
  display.setPartialWindow(0, 0, 400, 255);
  display.firstPage();
  do {
    int y = 10;
    for (int r = 0; r < amountOfBooks; r++) {
      display.setCursor(0, y);
      if (cursorIndex == r) {
        display.print("> ");
      }
      display.setCursor(10, y);
      display.println(books[r].name());
      y += 10;
    }
    movingCursor = false;
  } while (display.nextPage());
}

void drawPage() {
  books[bookIndex].seek(pageStart);

  display.setPartialWindow(0, 0, 400, 300);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    int line = 0;
    char currentLine[61];
    int lineLength = 0;
    currentLine[0] = '\0';
    while (books[bookIndex].available() && line < 13) {
      char c = books[bookIndex].read();
      if ((uint8_t)c == 0xE2) {
        char b2 = books[bookIndex].read();
        char b3 = books[bookIndex].read();
        if ((uint8_t)b2 == 0x80 && (uint8_t)b3 == 0x99) {
          c = '\'';
        }
        else if ((uint8_t)b2 == 0x80 &&
                 ((uint8_t)b3 == 0x9C || (uint8_t)b3 == 0x9D)) {
          c = '"';
        }
        else {
          c = ' ';
        }
      }
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        display.setCursor(10, 10 + line * 18);
        display.print(currentLine);
        line++;
        lineLength = 0;
        currentLine[0] = '\0';
        continue;
      }
      if (lineLength < 60) {
        currentLine[lineLength] = c;
        lineLength++;
        currentLine[lineLength] = '\0';
      }
      if (lineLength >= 60) {
        int breakPoint = -1;
        for (int i = lineLength - 1; i >= 0; i--) {
          if (currentLine[i] == ' ') {
            breakPoint = i;
            break;
          }
        }
        if (breakPoint >= 0) {
          currentLine[breakPoint] = '\0';
          display.setCursor(10, 10 + line * 18);
          display.print(currentLine);
          int remainingLength = lineLength - breakPoint - 1;
          for (int i = 0; i < remainingLength; i++) {
            currentLine[i] = currentLine[breakPoint + 1 + i];
          }
          lineLength = remainingLength;
          currentLine[lineLength] = '\0';
          line++;
        }
        else {
          display.setCursor(10, 10 + line * 18);
          display.print(currentLine);

          line++;
          lineLength = 0;
          currentLine[0] = '\0';
        }
      }
    }
    if (lineLength > 0 && line < 13) {
      display.setCursor(10, 10 + line * 18);
      display.print(currentLine);
    }
  } while (display.nextPage());
  pageStart = books[bookIndex].position();
  Serial.print("Ended at ");
  Serial.println(books[bookIndex].position());
}

int readPercentage(float batteryVoltage) {
  for (int i = 0; i < batValues - 1; i++) {
    if (batteryVoltage <= VOLTAGES[i] && batteryVoltage > VOLTAGES[i+1]) {
      float minVoltage = VOLTAGES[i];
      float maxVoltage = VOLTAGES[i+1];
      float minPercentage = PERCENTAGES[i];
      float maxPercentage = PERCENTAGES[i+1];
      return maxPercentage + (minPercentage - maxPercentage) * (batteryVoltage - maxVoltage) / (minVoltage - maxVoltage);
    }
  }
  if (batteryVoltage >= VOLTAGES[0]) {
    return 100;
  }
  return 0;
}

void setup() {
  Serial.begin(115200);
  pinMode(BAT_PIN, INPUT);
  pinMode(R_BUTTON, INPUT_PULLUP);
  pinMode(L_BUTTON, INPUT_PULLUP);
  pinMode(SELECT_BUTTON, INPUT_PULLUP);
  pinMode(SD_DET, INPUT);
  pinMode(SD_CS, OUTPUT);
  SD.begin(SD_CS, SPI);
  delay(2000);
  loadBooks();
  pagePositions[0] = 0;
  display.epd2.selectSPI(SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init();
  display.setRotation(0);
  display.setFullWindow();
  display.setTextSize(1);
  display.setTextColor(GxEPD_BLACK);
  drawMenu();
  lastActivityTime = millis();
}

void loop() {
  currentRbuttonState = digitalRead(R_BUTTON);
  currentLbuttonState = digitalRead(L_BUTTON);
  currentSelectButtonState = digitalRead(SELECT_BUTTON);

  if (displaySleeping) {
    if (currentRbuttonState == LOW ||
        currentLbuttonState == LOW ||
        currentSelectButtonState == LOW) {

      lastActivityTime = millis();
      wakeDisplay();
      while (digitalRead(R_BUTTON) == LOW ||
             digitalRead(L_BUTTON) == LOW ||
             digitalRead(SELECT_BUTTON) == LOW) {
        delay(10);
      }

      lastRbuttonState = HIGH;
      lastLbuttonState = HIGH;
      lastSelectButtonState = HIGH;

      return;
    }
    return;
  }
  if (millis() - lastActivityTime >= sleepTime) {
    sleepDisplay();
    return;
  }

  if (inMenu) {
    if (currentRbuttonState == LOW &&
        lastRbuttonState == HIGH &&
        millis() - lastButtonTime > debounceTime) {

      lastButtonTime = millis();
      lastActivityTime = millis();

      if (cursorIndex < amountOfBooks - 1) {
        movingCursor = true;
        cursorIndex++;
        drawMenu();
      }
    }

    if (currentLbuttonState == LOW &&
        lastLbuttonState == HIGH &&
        millis() - lastButtonTime > debounceTime) {

      lastButtonTime = millis();
      lastActivityTime = millis();

      if (cursorIndex > 0) {
        movingCursor = true;
        cursorIndex--;
        drawMenu();
      }
    }

    if (currentSelectButtonState == LOW &&
        lastSelectButtonState == HIGH &&
        millis() - lastButtonTime > debounceTime) {

      lastButtonTime = millis();
      lastActivityTime = millis();

      inMenu = false;
      bookIndex = cursorIndex;

      currentPage = 0;
      pageStart = 0;
      pagePositions[0] = 0;

      pageNeedsUpdate = true;
    }
  }

  else {

    if (currentRbuttonState == LOW &&
        lastRbuttonState == HIGH &&
        millis() - lastButtonTime > debounceTime) {

      lastButtonTime = millis();
      lastActivityTime = millis();
      pageNeedsUpdate = true;
    }

    if (currentLbuttonState == LOW &&
        lastLbuttonState == HIGH &&
        millis() - lastButtonTime > debounceTime) {

      lastButtonTime = millis();
      lastActivityTime = millis();

      if (currentPage > 0) {
        currentPage--;

        Serial.print("Left pressed, page=");
        Serial.println(currentPage);

        pageStart = pagePositions[currentPage];
        drawPage();
      }
    }

    if (currentSelectButtonState == LOW &&
        lastSelectButtonState == HIGH &&
        millis() - lastButtonTime > debounceTime) {

      lastButtonTime = millis();
      lastActivityTime = millis();

      inMenu = true;
      drawMenu();
    }

    if (pageNeedsUpdate) {
      drawPage();

      refreshCounter++;

      if (refreshCounter >= 10) {
        display.refresh();
        refreshCounter = 0;
      }

      Serial.print("Right pressed, page=");
      Serial.println(currentPage);

      pageNeedsUpdate = false;

      if (currentPage < 4999) {
        uint32_t nextPage = books[bookIndex].position();

        currentPage++;
        pagePositions[currentPage] = nextPage;
        pageStart = nextPage;
      }
    }
  }

  updateBar();

  lastRbuttonState = currentRbuttonState;
  lastLbuttonState = currentLbuttonState;
  lastSelectButtonState = currentSelectButtonState;
}