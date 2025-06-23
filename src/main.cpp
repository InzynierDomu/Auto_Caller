#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <driver/i2s.h>
#include <vector>

// Karta SD
#define SD_CS 5 // Pin CS dla karty SD

// I2S Wzmacniacz (MAX98357)
#define I2S_SPK_WS 25
#define I2S_SPK_BCK 26
#define I2S_SPK_DOUT 22
#define BUTTON_PIN 4 // Pin wejściowy przycisku (z pull-up)
#define OUTPUT_PIN1 12 // Pierwszy pin wyjściowy
#define OUTPUT_PIN2 14 // Drugi pin wyjściowy

#define SAMPLE_RATE 16000 // Częstotliwość próbkowania
#define BUFFER_SIZE 512 // Rozmiar bufora

enum SequenceState
{
  PIN1_ON, // PIN1 włączony na 10ms
  PAUSE1, // Pauza 20ms po PIN1
  PIN2_ON, // PIN2 włączony na 10ms
  PAUSE2 // Pauza 20ms po PIN2
};

// Zmienne globalne
SequenceState currentState = PIN1_ON;
unsigned long previousMillis = 0;
unsigned long pin1_interval = 2; // 10ms dla PIN1
unsigned long pause_interval = 25; // 20ms pauza
unsigned long pin2_interval = 2; // 10ms dla PIN2

bool outputState = false;

File audioFile;
bool isPlaying = false;

std::vector<String> fileNames;

// Zmienna do śledzenia czasu dla losowania
unsigned long lastRandomTime = 0;
const unsigned long randomInterval = 60000; // 1 minuta w milisekundach

void setupI2SSpeaker()
{
  i2s_config_t i2s_config = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
                             .sample_rate = SAMPLE_RATE,
                             .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
                             .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
                             .communication_format = I2S_COMM_FORMAT_I2S,
                             .intr_alloc_flags = 0,
                             .dma_buf_count = 8,
                             .dma_buf_len = BUFFER_SIZE,
                             .use_apll = false};
  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_SPK_BCK, .ws_io_num = I2S_SPK_WS, .data_out_num = I2S_SPK_DOUT, .data_in_num = I2S_PIN_NO_CHANGE};
  i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pin_config);
}

// Funkcja do odtwarzania nagrania
void playAudio()
{
  Serial.println("Odtwarzanie...");
  audioFile = SD.open("/recording.wav", FILE_READ);
  if (!audioFile)
  {
    Serial.println("Błąd otwierania pliku!");
    return;
  }

  isPlaying = true;
  size_t bytesRead;
  int16_t buffer[BUFFER_SIZE];

  while (audioFile.available())
  {
    bytesRead = audioFile.read((uint8_t*)buffer, BUFFER_SIZE * sizeof(int16_t));
    i2s_write(I2S_NUM_1, buffer, bytesRead, &bytesRead, portMAX_DELAY);
  }

  audioFile.close();
  isPlaying = false;
  Serial.println("Odtwarzanie zakończone.");
}

void listDir(fs::FS& fs, const char* dirname, uint8_t levels)
{
  Serial.printf("Otwieranie katalogu: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root)
  {
    Serial.println("Błąd: nie można otworzyć folderu");
    return;
  }
  if (!root.isDirectory())
  {
    Serial.println("To nie jest katalog");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
      Serial.print("DIR: ");
      Serial.println(file.name());
      if (levels)
      {
        listDir(fs, file.path(), levels - 1);
      }
    }
    else
    {
      Serial.print("FILE: ");
      Serial.print(file.name());
      Serial.print("  ROZMIAR: ");
      Serial.println(file.size());

      // Dodaj nazwę pliku do globalnego wektora
      fileNames.push_back(String(file.name()));
    }
    file = root.openNextFile();
  }
}

void read_config()
{
  File configFile = SD.open("/config.txt");
  if (!configFile)
  {
    Serial.println("Nie udało się otworzyć config.txt");
    return;
  }

  // Odczyt danych jako tekst
  String configValue = configFile.readStringUntil('\n');
  configFile.close();

  // Konwersja na liczbę
  int number = configValue.toInt();

  Serial.print("Wartość odczytana z pliku: ");
  Serial.println(number);
}

String getRandomFileName()
{
  // Sprawdź czy wektor nie jest pusty
  if (fileNames.size() == 0)
  {
    Serial.println("Błąd: brak plików w wektorze!");
    return "";
  }

  // Wylosuj indeks z przedziału 0 do (rozmiar_wektora - 1)
  int randomIndex = random(0, fileNames.size());

  Serial.printf("Wylosowano plik o indeksie %d: %s\n", randomIndex, fileNames[randomIndex].c_str());

  return fileNames[randomIndex];
}

void setup()
{
  pinMode(BUTTON_PIN, INPUT_PULLUP); // Przycisk z wewnętrznym pull-up
  pinMode(OUTPUT_PIN1, OUTPUT); // Pierwszy pin wyjściowy
  pinMode(OUTPUT_PIN2, OUTPUT); // Drugi pin wyjściowy

  // Początkowy stan - oba wyjścia wyłączone
  digitalWrite(OUTPUT_PIN1, LOW);
  digitalWrite(OUTPUT_PIN2, LOW);

  // for debuging
  Serial.begin(115200);
  while (!Serial)
  {}

  // Inicjalizacja generatora liczb losowych
  randomSeed(analogRead(0));

  if (!SD.begin(SD_CS))
  {
    Serial.println("Błąd inicjalizacji karty SD!");
    return;
  }
  else
  {
    Serial.println("SD załadowana");
  }

  setupI2SSpeaker();

  listDir(SD, "/records", 0);

  read_config();

  // Ustaw początkowy czas
  lastRandomTime = millis();
}

void loop()
{
  bool buttonPressed = (digitalRead(BUTTON_PIN) == LOW);

  if (buttonPressed)
  {
    // Przycisk naciśnięty - obsługa sekwencji
    unsigned long currentMillis = millis();
    unsigned long currentInterval;

    // Określ aktualny interwał na podstawie stanu
    switch (currentState)
    {
      case PIN1_ON:
        currentInterval = pin1_interval;
        break;
      case PAUSE1:
        currentInterval = pause_interval;
        break;
      case PIN2_ON:
        currentInterval = pin2_interval;
        break;
      case PAUSE2:
        currentInterval = pause_interval;
        break;
    }

    if (currentMillis - previousMillis >= currentInterval)
    {
      previousMillis = currentMillis;

      // Przejście do następnego stanu
      switch (currentState)
      {
        case PIN1_ON:
          // PIN1 był włączony, teraz pauza
          digitalWrite(OUTPUT_PIN1, LOW);
          digitalWrite(OUTPUT_PIN2, LOW);
          currentState = PAUSE1;
          break;

        case PAUSE1:
          // Koniec pauzy po PIN1, włącz PIN2
          digitalWrite(OUTPUT_PIN1, LOW);
          digitalWrite(OUTPUT_PIN2, HIGH);
          currentState = PIN2_ON;
          break;

        case PIN2_ON:
          // PIN2 był włączony, teraz pauza
          digitalWrite(OUTPUT_PIN1, LOW);
          digitalWrite(OUTPUT_PIN2, LOW);
          currentState = PAUSE2;
          break;

        case PAUSE2:
          // Koniec pauzy po PIN2, wróć do PIN1
          digitalWrite(OUTPUT_PIN1, HIGH);
          digitalWrite(OUTPUT_PIN2, LOW);
          currentState = PIN1_ON;
          break;
      }
    }
  }
  else
  {
    // Przycisk nie naciśnięty - wyłącz oba wyjścia
    digitalWrite(OUTPUT_PIN1, LOW);
    digitalWrite(OUTPUT_PIN2, LOW);

    // Reset stanu dla następnego naciśnięcia
    currentState = PIN1_ON;
    previousMillis = millis(); // Reset timera
  }

  // if (currentTime - lastRandomTime >= randomInterval)
  // {
  //   // Wylosuj i wypisz nazwę pliku
  //   String randomName = getRandomFileName();
  //   if (randomName != "")
  //   {
  //     Serial.println("=== LOSOWANIE CO MINUTĘ ===");
  //     Serial.println("Wylosowana nazwa: " + randomName);
  //     Serial.println("===========================");
  //   }

  //   // Zaktualizuj czas ostatniego losowania
  //   lastRandomTime = currentTime;
  // }

  // Tutaj możesz dodać inne zadania loop
  // np. obsługa przycisków, czujników itp.
}