
#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>

// --- Configuration ---
const char* STATION_SSID = "YOUR_WIFI_SSID";
const char* STATION_PASSWORD = "YOUR_WIFI_PASSWORD";
// Crucial: Use your actual Render URL (exclude trailing slash)
const char* RENDER_SERVER_URL = "https://your-app-name.onrender.com"; 

// --- Pin Definitions ---
const int PTT_BUTTON_PIN = 4;   // Momentary Push-To-Talk Button (Active LOW)
const int LED_TX_PIN = 5;       // TX Indicator LED
const int LED_RX_PIN = 6;       // RX Indicator LED

// I2S Microphone Pins (INMP441)
#include <driver/i2s.h>
#define I2S_MIC_WS   41
#define I2S_MIC_SD   42
#define I2S_MIC_SCK  40

// I2S Speaker Pins (MAX98357A)
#define I2S_SPEAKER_WS   14
#define I2S_SPEAKER_DOUT 13
#define I2S_SPEAKER_BCLK 12

// --- Audio Settings ---
#define SAMPLE_RATE     16000   // 16kHz is standard for clear voice comms
#define BUFFER_SIZE     1024    // Size of the audio chunk to stream
uint8_t audioBuffer[BUFFER_SIZE];

void setup() {
  Serial.begin(115200);
  
  pinMode(PTT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_TX_PIN, OUTPUT);
  pinMode(LED_RX_PIN, OUTPUT);
  
  // Connect to Wi-Fi
  WiFi.begin(STATION_SSID, STATION_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Network!");
  
  // Initialize I2S Hardware Pipelines
  initI2SMic();
  initI2SSpeaker();
}

void loop() {
  // Check if PTT Button is pressed (Active LOW)
  if (digitalRead(PTT_BUTTON_PIN) == LOW) {
    handleTransmission();
  } else {
    handleReception();
  }
}

// --- I2S Drivers Initialization ---

void initI2SMic() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), // RX means receive from Mic
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // INMP441 is mono
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_MIC_SCK,
    .ws_io_num = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void initI2SSpeaker() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX), // TX means output to Speaker
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SPEAKER_BCLK,
    .ws_io_num = I2S_SPEAKER_WS,
    .data_out_num = I2S_SPEAKER_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pin_config);
}

// --- Core CBMR Modes ---

void handleTransmission() {
  digitalWrite(LED_TX_PIN, HIGH);
  digitalWrite(LED_RX_PIN, LOW);
  
  size_t bytesRead = 0;
  // Read raw audio from the digital microphone DMA buffers
  i2s_read(I2S_NUM_0, &audioBuffer, BUFFER_SIZE, &bytesRead, portMAX_DELAY);
  
  if (bytesRead > 0 && WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String targetUrl = String(RENDER_SERVER_URL) + "/transmit";
    http.begin(targetUrl);
    
    // Set headers for binary byte-stream payload
    http.addHeader("Content-Type", "application/octet-stream");
    
    // Shoot the raw data up to Render
    int httpResponseCode = http.POST(audioBuffer, bytesRead);
    
    if (httpResponseCode == 200) {
      Serial.println("[TX] Audio packet dispatched successfully.");
    } else {
      Serial.printf("[TX] Failed sending audio. Code: %d\n", httpResponseCode);
    }
    http.end();
  }
}

void handleReception() {
  digitalWrite(LED_TX_PIN, LOW);
  
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String targetUrl = String(RENDER_SERVER_URL) + "/receive";
    http.begin(targetUrl);
    
    int httpResponseCode = http.GET();
    
    // 200 means there is an audio packet waiting for us
    if (httpResponseCode == 200) {
      digitalWrite(LED_RX_PIN, HIGH);
      
      WiFiClient* stream = http.getStreamPtr();
      size_t bytesWritten = 0;
      
      // Keep pulling data chunks from the network stream and pipe them directly into the speaker
      while (http.connected() && stream->available()) {
        int len = stream->readBytes(audioBuffer, sizeof(audioBuffer));
        i2s_write(I2S_NUM_1, &audioBuffer, len, &bytesWritten, portMAX_DELAY);
      }
    } 
    // 204 means the server is active but no one is transmitting right now
    else if (httpResponseCode == 204) {
      digitalWrite(LED_RX_PIN, LOW); // Stay quiet
    }
    
    http.end();
  }
  
  // Small breathing room to avoid hitting Render hundreds of times per second while idling
  delay(100); 
}
