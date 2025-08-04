#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <driver/i2s.h>
#include <vector>

const uint8_t sd_cs_pin = 5;

const uint8_t speaker_ws_pin = 25;
const uint8_t speaker_bck_pin = 26;
const uint8_t speaker_dout_pin = 22;
const uint8_t limit_switch_pin = 4;
const uint8_t bell1_pin = 12;
const uint8_t bell2_pin = 14;

enum class system_state
{
  IDLE,
  RINGING,
  AUDIO_PLAY
};

system_state currentState = system_state::IDLE;

enum class sequence_state
{
  IDLE,
  PIN1_ON,
  PAUSE1,
  PIN2_ON,
  PAUSE2
};

sequence_state sequenceState = sequence_state::IDLE;

enum class bell_state
{
  IDLE,
  RING,
  PAUSE
};

bell_state bell = bell_state::IDLE;

const uint16_t buffer_size = 512;
uint16_t sample_rate = 16000;

unsigned long sequenceTimestamp = 0;
const uint8_t pin1_duration = 2;
const uint8_t pause_duration = 25;
const uint8_t pin2_duration = 2;
const unsigned long ringing_pause_time = 4000;
unsigned long pause_time = 0;
const int cycles_num = 3;
int current_cycles = 0;

unsigned long debounce_filter_time = 10;
bool buttonPressed = false;

bool outputState = false;

File audioFile;
bool isPlaying = false;

String file;

std::vector<String> fileNames;

unsigned long lastRandomTime = 0;
unsigned long randomInterval = 60000;

unsigned long sequenceStartTime = 0;
const unsigned long maxSequenceDuration = 2000;

void setupI2SSpeaker()
{
  Serial.println("audio cofing start");
  i2s_config_t i2s_config = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
                             .sample_rate = sample_rate,
                             .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
                             .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
                             .communication_format = I2S_COMM_FORMAT_I2S,
                             .intr_alloc_flags = 0,
                             .dma_buf_count = 8,
                             .dma_buf_len = buffer_size,
                             .use_apll = false};
  i2s_pin_config_t pin_config = {
      .bck_io_num = speaker_bck_pin, .ws_io_num = speaker_ws_pin, .data_out_num = speaker_dout_pin, .data_in_num = I2S_PIN_NO_CHANGE};
  esp_err_t err = i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
  Serial.println(esp_err_to_name(err));
  err = i2s_set_pin(I2S_NUM_1, &pin_config);
  Serial.println(esp_err_to_name(err));
  Serial.println("audio cofing end");
}

void playAudio()
{
  Serial.println("playing audio...");
  String file_path = "/records/" + file;
  audioFile = SD.open(file_path, FILE_READ);
  if (!audioFile)
  {
    Serial.println("error with audio file");
    return;
  }

  isPlaying = true;
  i2s_start(I2S_NUM_1);
  size_t bytesRead;
  int16_t buffer[buffer_size];

  while (audioFile.available())
  {
    bytesRead = audioFile.read((uint8_t*)buffer, buffer_size * sizeof(int16_t));
    i2s_write(I2S_NUM_1, buffer, bytesRead, &bytesRead, portMAX_DELAY);
  }

  audioFile.close();

  i2s_stop(I2S_NUM_1);

  isPlaying = false;
  Serial.println("playing end.");
}

void listDir(fs::FS& fs, const char* dirname, uint8_t levels)
{
  Serial.printf("open dir: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root)
  {
    Serial.println("error: cant open the folder");
    return;
  }
  if (!root.isDirectory())
  {
    Serial.println("this isn't a folder");
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
      Serial.print("file: ");
      Serial.print(file.name());
      Serial.print("  size: ");
      Serial.println(file.size());

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
    Serial.println("can't open config.txt");
    return;
  }

  String intervalLine = configFile.readStringUntil('\n');
  String rateLine = configFile.readStringUntil('\n');
  configFile.close();

  unsigned long interval = intervalLine.toInt();
  unsigned long rate = rateLine.toInt();

  if (interval > 0)
  {
    randomInterval = interval;
  }

  if (rate > 0)
  {
    sample_rate = rate;
  }

  Serial.println("Load config:");
  Serial.print("random interval = ");
  Serial.println(randomInterval);
  Serial.print("sample_rate = ");
  Serial.println(sample_rate);
}

String getRandomFileName()
{
  if (fileNames.size() == 0)
  {
    Serial.println("error: no audio file");
    return "";
  }

  // Wylosuj indeks z przedziału 0 do (rozmiar_wektora - 1)
  int randomIndex = random(0, fileNames.size());

  Serial.printf("file with number %d: %s\n", randomIndex, fileNames[randomIndex].c_str());

  return fileNames[randomIndex];
}

void startPinSequence()
{
  sequenceState = sequence_state::PIN1_ON;
  sequenceTimestamp = millis();
  sequenceStartTime = millis();
  digitalWrite(bell1_pin, HIGH);
  digitalWrite(bell2_pin, LOW);
  Serial.println("ringing...");
}

void stopPinSequence()
{
  sequenceState = sequence_state::IDLE;
  digitalWrite(bell1_pin, LOW);
  digitalWrite(bell2_pin, LOW);
  current_cycles++;
  pause_time = millis();
  Serial.println("stop ringing");
  bell = bell_state::PAUSE;
}

void handleSequence()
{
  unsigned long now = millis();

  if (now - sequenceStartTime > maxSequenceDuration)
  {
    stopPinSequence();
    lastRandomTime = now;
    return;
  }

  switch (sequenceState)
  {
    case sequence_state::PIN1_ON:
      if (now - sequenceTimestamp >= pin1_duration)
      {
        digitalWrite(bell1_pin, LOW);
        sequenceState = sequence_state::PAUSE1;
        sequenceTimestamp = now;
      }
      break;

    case sequence_state::PAUSE1:
      if (now - sequenceTimestamp >= pause_duration)
      {
        digitalWrite(bell2_pin, HIGH);
        sequenceState = sequence_state::PIN2_ON;
        sequenceTimestamp = now;
      }
      break;

    case sequence_state::PIN2_ON:
      if (now - sequenceTimestamp >= pin2_duration)
      {
        digitalWrite(bell2_pin, LOW);
        sequenceState = sequence_state::PAUSE2;
        sequenceTimestamp = now;
      }
      break;

    case sequence_state::PAUSE2:
      if (now - sequenceTimestamp >= pause_duration)
      {
        digitalWrite(bell1_pin, HIGH);
        sequenceState = sequence_state::PIN1_ON;
        sequenceTimestamp = now;
      }
      break;

    case sequence_state::IDLE:
      break;
  }
}

void startMasterSequence()
{
  bell = bell_state::RING;
  current_cycles = 0;
  Serial.println("Master sequence started");
  startPinSequence();
}

void handleMasterSequence()
{
  switch (bell)
  {
    case bell_state::IDLE:
      break;

    case bell_state::RING:
      handleSequence();
      if (current_cycles == cycles_num)
      {
        Serial.println("Master sequence done.");
        bell_state::IDLE;
        currentState = system_state::IDLE;
      }
      break;

    case bell_state::PAUSE:
      if (millis() - pause_time >= ringing_pause_time)
      {
        bell = bell_state::RING;
        startPinSequence();
      }
      break;
  }
}

void checkButton()
{
  static unsigned long last_button_change_time = 0;
  static bool last_button_state = HIGH;
  bool reading = digitalRead(limit_switch_pin);

  if (reading != last_button_state)
  {
    last_button_change_time = millis();
  }
  if ((millis() - last_button_change_time) > debounce_filter_time && reading == LOW)
  {
    Serial.println("audio");
    stopPinSequence();
    currentState = system_state::AUDIO_PLAY;
  }
  last_button_state = reading;
}

void setup()
{
  pinMode(limit_switch_pin, INPUT_PULLUP);
  pinMode(bell1_pin, OUTPUT);
  pinMode(bell2_pin, OUTPUT);

  digitalWrite(bell1_pin, LOW);
  digitalWrite(bell2_pin, LOW);

  Serial.begin(115200);

  randomSeed(analogRead(0));

  if (!SD.begin(sd_cs_pin))
  {
    Serial.println("SD card error");
    return;
  }
  else
  {
    Serial.println("SD card ok");
  }

  listDir(SD, "/records", 0);
  read_config();

  setupI2SSpeaker();

  lastRandomTime = millis();
  Serial.println("setup end");
}

void loop()
{
  unsigned long now = millis();

  switch (currentState)
  {
    case system_state::IDLE:
      if ((now - lastRandomTime >= randomInterval) && (digitalRead(limit_switch_pin) == HIGH))
      {
        file = getRandomFileName();
        if (file != "")
        {
          Serial.print("file name: ");
          Serial.println(file);
        }
        startMasterSequence();
        currentState = system_state::RINGING;
      }
      break;

    case system_state::RINGING:
      checkButton();
      handleMasterSequence();
      break;

    case system_state::AUDIO_PLAY:
      playAudio();
      currentState = system_state::IDLE;
      lastRandomTime = millis();
      break;
  }
}