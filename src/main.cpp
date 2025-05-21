#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <driver/i2s.h>

// Karta SD
#define SD_CS 5 // Pin CS dla karty SD

// I2S Wzmacniacz (MAX98357)
#define I2S_SPK_WS 25
#define I2S_SPK_BCK 26
#define I2S_SPK_DOUT 22

#define SAMPLE_RATE 16000 // Częstotliwość próbkowania
#define BUFFER_SIZE 512 // Rozmiar bufora

File audioFile;
bool isPlaying = false;

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

void setup()
{
  // for debuging
  Serial.begin(115200);
  while (!Serial)
  {}

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
}

void loop()
{
  // put your main code here, to run repeatedly:
}
