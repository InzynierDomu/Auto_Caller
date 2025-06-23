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
  IDLE, // Stan spoczynku
  PIN1_ON, // PIN1 włączony na 10ms
  PAUSE1, // Pauza 20ms po PIN1
  PIN2_ON, // PIN2 włączony na 10ms
  PAUSE2 // Pauza 20ms po PIN2
};

// Zmienne globalne dla sekwencji
SequenceState currentSequenceState = IDLE;
unsigned long previousMillisSequence = 0;
unsigned long pin1_duration = 2; // 10ms dla PIN1
unsigned long pause_duration = 25; // 20ms pauza
unsigned long pin2_duration = 2; // 10ms dla PIN2

bool outputState = false;

File audioFile;
bool isPlaying = false;

std::vector<String> fileNames;

// Zmienne globalne dla ogólnego timera (co minutę)
unsigned long lastRandomTime = 0;
const unsigned long randomInterval = 60000; // 1 minuta w milisekundach

// Zmienne globalne dla timera zakończenia sekwencji (5 sekund)
unsigned long sequenceStartTime = 0;
const unsigned long maxSequenceDuration = 5000; // 5 sekund w milisekundach

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

void startPinSequence()
{
  Serial.println("Aktywacja sekwencji pinów.");
  currentSequenceState = PIN1_ON;
  previousMillisSequence = millis(); // Resetuj timer dla przejść w sekwencji
  sequenceStartTime = millis(); // Zapisz czas rozpoczęcia całej sekwencji
  digitalWrite(OUTPUT_PIN1, HIGH); // Włącz PIN1 od razu
  digitalWrite(OUTPUT_PIN2, LOW);
}

void stopPinSequence()
{
  if (currentSequenceState != IDLE)
  {
    Serial.println("Sekwencja zakończona.");
    digitalWrite(OUTPUT_PIN1, LOW);
    digitalWrite(OUTPUT_PIN2, LOW);
    currentSequenceState = IDLE;
  }
}

void handlePinSequence()
{
  unsigned long currentMillis = millis();
  unsigned long currentDuration;

  // Sprawdź, czy sekwencja już trwa dłużej niż 5 sekund
  if (currentMillis - sequenceStartTime >= maxSequenceDuration)
  {
    stopPinSequence(); // Zatrzymaj sekwencję, jeśli przekroczono 5 sekund
    return;
  }

  // Określ aktualny czas trwania na podstawie stanu
  switch (currentSequenceState)
  {
    case PIN1_ON:
      currentDuration = pin1_duration;
      break;
    case PAUSE1:
      currentDuration = pause_duration;
      break;
    case PIN2_ON:
      currentDuration = pin2_duration;
      break;
    case PAUSE2:
      currentDuration = pause_duration;
      break;
    case IDLE: // Nie powinno się tutaj znaleźć, ale zabezpieczenie
      return;
  }

  if (currentMillis - previousMillisSequence >= currentDuration)
  {
    previousMillisSequence = currentMillis;

    // Przejście do następnego stanu
    switch (currentSequenceState)
    {
      case PIN1_ON:
        // PIN1 był włączony, teraz pauza
        digitalWrite(OUTPUT_PIN1, LOW);
        digitalWrite(OUTPUT_PIN2, LOW);
        currentSequenceState = PAUSE1;
        break;

      case PAUSE1:
        // Koniec pauzy po PIN1, włącz PIN2
        digitalWrite(OUTPUT_PIN1, LOW);
        digitalWrite(OUTPUT_PIN2, HIGH);
        currentSequenceState = PIN2_ON;
        break;

      case PIN2_ON:
        // PIN2 był włączony, teraz pauza
        digitalWrite(OUTPUT_PIN1, LOW);
        digitalWrite(OUTPUT_PIN2, LOW);
        currentSequenceState = PAUSE2;
        break;

      case PAUSE2:
        // Koniec pauzy po PIN2, wróć do PIN1 (sekwencja zapętla się)
        // Jeśli chcemy, aby sekwencja zatrzymywała się po jednym przebiegu,
        // zmieniamy to na stopPinSequence();
        digitalWrite(OUTPUT_PIN1, HIGH);
        digitalWrite(OUTPUT_PIN2, LOW);
        currentSequenceState = PIN1_ON; // Zapętlenie sekwencji
        // previousMillisSequence = currentMillis; // Można zresetować timer tutaj dla świeżego startu nowego cyklu w ramach 5s
        break;
      case IDLE:
        break; // Niewykorzystane, ale dla kompletności
    }
  }
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
  unsigned long currentTime = millis();

  // Obsługa przycisku - jeśli naciśnięty, zatrzymaj sekwencję (jeśli aktywna)
  // Przycisk działa jak "wyłącznik" / "reset"
  static bool lastButtonState = HIGH; // Pamiętaj poprzedni stan przycisku
  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (currentButtonState == LOW && lastButtonState == HIGH) // Przycisk został naciśnięty (zbocze opadające)
  {
    Serial.println("Przycisk naciśnięty - zatrzymanie sekwencji.");
    stopPinSequence(); // Zatrzymaj sekwencję
  }
  lastButtonState = currentButtonState;

  // Sekcja, która będzie aktywować sekwencję co minutę
  if (currentTime - lastRandomTime >= randomInterval)
  {
    // Wylosuj i wypisz nazwę pliku (teraz to reprezentuje "numer")
    String randomName = getRandomFileName();
    if (randomName != "")
    {
      Serial.println("=== LOSOWANIE CO MINUTĘ ===");
      Serial.println("Wylosowana nazwa/numer: " + randomName);
      Serial.println("===========================");
    }

    // Aktywuj sekwencję pinów, jeśli nie jest już aktywna
    if (currentSequenceState == IDLE)
    { // Upewnij się, że nie uruchamiasz nowej sekwencji, jeśli poprzednia jeszcze działa
      startPinSequence();
    }

    // Zaktualizuj czas ostatniego losowania (następne losowanie za minutę)
    lastRandomTime = currentTime;
  }

  // Jeśli sekwencja jest aktywna (czyli nie jest w stanie IDLE), to ją obsługuj
  // Oraz sprawdzaj warunek zakończenia 5s lub przyciskiem
  if (currentSequenceState != IDLE)
  {
    handlePinSequence();
  }
}