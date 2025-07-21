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


// ------------------------------ Stany FSM ------------------------------
enum SystemState
{
  STATE_IDLE,
  STATE_RINGING,
  STATE_AUDIO_PLAY
};

SystemState currentState = STATE_IDLE;

// ------------------------------ Stany sekwencji pinów ------------------------------
enum SequenceState
{
  SEQ_IDLE,
  SEQ_PIN1_ON,
  SEQ_PAUSE1,
  SEQ_PIN2_ON,
  SEQ_PAUSE2
};

SequenceState sequenceState = SEQ_IDLE;

unsigned long sequenceTimestamp = 0;
const unsigned long pin1_duration = 2;
const unsigned long pause_duration = 25;
const unsigned long pin2_duration = 2;

unsigned long buttonDebounceMillis = 0;
bool buttonPressed = false;

bool outputState = false;

File audioFile;
bool isPlaying = false;

std::vector<String> fileNames;

unsigned long lastRandomTime = 0;
const unsigned long randomInterval = 60000; // 1 minuta

unsigned long sequenceStartTime = 0;
const unsigned long maxSequenceDuration = 5000; // 5 sekund

void setupI2SSpeaker()
{
  Serial.println("audio cofing start");
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
  esp_err_t err = i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
  Serial.println(esp_err_to_name(err));
  err = i2s_set_pin(I2S_NUM_1, &pin_config);
  Serial.println(esp_err_to_name(err));
  Serial.println("audio cofing end");
}

// Funkcja do odtwarzania nagrania
void playAudio()
{
  Serial.println("Odtwarzanie...");
  audioFile = SD.open("/records/test1.wav", FILE_READ);
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
  sequenceState = SEQ_PIN1_ON;
  sequenceTimestamp = millis();
  sequenceStartTime = millis();
  digitalWrite(OUTPUT_PIN1, HIGH);
  digitalWrite(OUTPUT_PIN2, LOW);
  Serial.println("Start dzwonienia...");
}

void stopPinSequence()
{
  sequenceState = SEQ_IDLE;
  digitalWrite(OUTPUT_PIN1, LOW);
  digitalWrite(OUTPUT_PIN2, LOW);
  Serial.println("Zatrzymanie dzwonienia.");
}

void handleSequence()
{
  unsigned long now = millis();

  // Max czas
  if (now - sequenceStartTime > maxSequenceDuration)
  {
    stopPinSequence();
    currentState = STATE_IDLE;
    lastRandomTime = now;
    return;
  }

  switch (sequenceState)
  {
    case SEQ_PIN1_ON:
      if (now - sequenceTimestamp >= pin1_duration)
      {
        digitalWrite(OUTPUT_PIN1, LOW);
        sequenceState = SEQ_PAUSE1;
        sequenceTimestamp = now;
      }
      break;

    case SEQ_PAUSE1:
      if (now - sequenceTimestamp >= pause_duration)
      {
        digitalWrite(OUTPUT_PIN2, HIGH);
        sequenceState = SEQ_PIN2_ON;
        sequenceTimestamp = now;
      }
      break;

    case SEQ_PIN2_ON:
      if (now - sequenceTimestamp >= pin2_duration)
      {
        digitalWrite(OUTPUT_PIN2, LOW);
        sequenceState = SEQ_PAUSE2;
        sequenceTimestamp = now;
      }
      break;

    case SEQ_PAUSE2:
      if (now - sequenceTimestamp >= pause_duration)
      {
        digitalWrite(OUTPUT_PIN1, HIGH);
        sequenceState = SEQ_PIN1_ON;
        sequenceTimestamp = now;
      }
      break;

    case SEQ_IDLE:
      // Nic nie rób
      break;
  }
}

void checkButton()
{
  // bool currentState = digitalRead(BUTTON_PIN);
  // if (currentState && !buttonPressed && millis() - buttonDebounceMillis > 10)
  // {
  //   buttonPressed = true;
  //   buttonDebounceMillis = millis();
  // }

  if (digitalRead(BUTTON_PIN) == LOW && currentState == STATE_RINGING)
  {
    // buttonPressed = false;

    // // Obsługa przycisku zależnie od stanu
    // if (currentState == STATE_RINGING)
    // {
    Serial.println("audio");
    stopPinSequence();
    currentState = STATE_AUDIO_PLAY;
    // }
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
  // while (!Serial)
  // {}

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
  Serial.println("setup end");
}

void loop()
{
  checkButton();

  unsigned long now = millis();

  switch (currentState)
  {
    case STATE_IDLE:
      if (now - lastRandomTime >= randomInterval)
      {
        String file = getRandomFileName();
        if (file != "")
        {
          Serial.print("Wylosowano plik: ");
          Serial.println(file);
        }
        startPinSequence();
        currentState = STATE_RINGING;
      }
      break;

    case STATE_RINGING:
      handleSequence();
      break;

    case STATE_AUDIO_PLAY:
      playAudio();
      currentState = STATE_IDLE;
      lastRandomTime = millis();
      break;
  }
}