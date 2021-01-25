// Agradecimentos a Filipe Nicoli

#include "ani.h" //animação
#include <SPI.h>
#include <TFT_eSPI.h>    // Hardware-specific library
#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson.git
#include <NTPClient.h>   //https://github.com/taranais/NTPClient
#include "driver/rtc_io.h"
#include "Orbitron_Medium_20.h" //fonte
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include "FreeSans7pt7b.h" // Fonte (caracteres com acentos, principalmente)
#include "OpenSans-Light.h"
// #include "BluetoothSerial.h"

// Informações para debug via serial
#define DEBUG
#ifdef DEBUG
#define DEBUG_CORE0
#define DEBUG_CORE1
#endif
int breakpoint = 1, atraso = 0;
byte espacamento = 50;
bool tab = false;

// Cores
#define TFT_GREY 0x5AEB
#define lightblue 0x01E9
#define darkred 0xA041
#define blue 0x5D9B
#define turquesa 0x471a
#define tiffany 0x46da
#define deep_sky_blue 0x061f

// Informações do backlight
const int pwmFreq = 100;
const int pwmResolution = 8;
const int pwmLedChannelTFT = 0;

// Informações da wifi
char *ssid = "ruvian"; //*ssid é o primeiro caractere. ssid é o endereço da string
char *password = "ruviandc";
String town = "Carlos Barbosa";
String Country = "BR";
const String link1 = "http://api.openweathermap.org/data/2.5/weather?q=" + town + "," + Country + "&units=metric&APPID=";
const String link2 = "http://api.openweathermap.org/data/2.5/find?lat=-29.2940&lon=-51.5036&cnt=1&units=metric&APPID=";
const String key = "b9be9458d695aa6dbaa599c2898dff1a"; //chave que o site te dá

// JSON - arrumar porque tem coisa a mais ou a menos
StaticJsonDocument<600> doc;
String payload1 = ""; //whole json
String payload2 = "";

// Variables to save date and time
String formattedDate;
char dayStamp[20], timeStamp[6], secondStamp[3], tt[10], curSeconds[3];

// Intensidades do backlight
int backlight[5] = {10, 30, 60, 120, 255};
RTC_DATA_ATTR uint8_t b = 4; //começa em 255

// Botões
#define BOTAO_ESQUERDO 0
#define BOTAO_DIREITO 35
uint8_t Threshold_Touch = 80; // Quanto menor, menor a sensibilidade

// Handlers de task
TaskHandle_t ShowPageWeather_1, GetInfo, print_TFT, Core_0, Core_1, manutencao_TempUmid, attachInterrupt_GetData, attachInterrupt_Display;

// Mutex handlers
SemaphoreHandle_t Mutex, writeTFT;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Outras variáveis
int i = 0, count = 0, frame = 0;
bool firstTime = 1, sleep_agreement = 0;
double TempoConexaoWifi;
RTC_DATA_ATTR bool AcabouDeIniciar = true;
// bool s_pad_activated[10];
struct timeval TempoSistema; // O que tem sec e usec
struct tm DataSistema;       // Data completa
TFT_eSPI tft = TFT_eSPI();   // Invoke custom library
bool WasUpdated = false, BT_enabled = false, printed_page = false;
byte page;
int httpCode, x_cursor, y_cursor;
int32_t milis, general_limiter = -500, limiter1 = -500, limiter2 = -5500;

#define PageWeather_TempHumi 1
#define PageWeather_WindCloud 2

#define TFT_WIDTH_3_4 100
#define TFT_WIDTH_1_2 
#define TFT_WIDTH_1_4 33

#define TOUCH_DELAY 500

struct Hardware_timer
{
  hw_timer_t *printValues = NULL, *turnOffBT = NULL;
} Timer;

struct Physical_quantity
{
  String rain, description, main, gust, cloudiness, town, temperature, humidity, max_temperature, min_temperature, feels_like, wind_speed;
  uint wind_degree;
  struct tm struct_last_updated; // O que tem sec e usec
  time_t updated_dt;
  char timeStamp[6];
} Data;

struct text
{
  String texto[6];
  String numbers[6];
};

text Screen1, Screen2;

// BluetoothSerial BT;

/* ############### Informações importantes ###############

 Clock - diminuir clock pra 10 MHz quando não usar wifi
 Sleep - sleep só quando apagar a tela (timer)

 Brilho - Não dá pra manter a tela ligada (a não ser brilho 100%) com algum sleep mode
 Wifi - Não dá pra usar wifi com menos de 80 MHz
 Obs - Os timers são alterados se a frequência da CPU for menor que 80 MHz, mas não são afetados se for maior

 getData() no core 0, e atualizar a tela no core 1
 SOMENTE o core 1 escreve na tela. Criar tasks toda vez que o core 0 precisar escrever algo na tela
 Prioridades das tasks - O sistema de prioridades é invertida em relação aos números (crescentes). Usar 1 para a prioridade máxima

 Width e height - O display vai de 0 a 134 em largura e 0 a 239 em altura

 Wakeup - esp_sleep_enable_touchpad_wakeup sobrepõe esp_sleep_enable_ext0_wakeup, ou seja, acorda por touch XOR por botão físico

 Fontes - As fontes padrões (setTextFont()) não possuem acentos

 Prioridade dos timers - As prioridades dos timers são seus IDs e também são inversas (0 é o maior nível)

 Touch button - O touch está configurado para gerar apenas 1 callback por toque. Se houver toque contínuo, há um delay 

 Não tem como deixar o ESP dormindo enquanto a tela estiver ligada e querer que tenha os botões touch funcionando
 porque quando ele acordar vai piscar a tela e isso fica feio, além do PWM do display só poder ser feito quando acordado

 -Ver de adicionar o bluetooth
 -Implementar quebras de linha sem quebrar palavras

 *Wifi scan e array de redes conhecidas
 *Ícone de bateria e uso de ADC
 *Calibração de ADC
 *Páginas de notas e condições climáticas
 *Mais condições climáticas na página
 *Request para 5 cidades em círculo http://api.openweathermap.org/data/2.5/find?lat=-29.2940&lon=-51.5036&cnt=5&APPID=b9be9458d695aa6dbaa599c2898dff1a

Funções dos botões touch
1-Busca wifi disponível e atualiza
2-Liga comunicação Bluetooth
3-Fotos
4-Página anterior da tela

5-Acorda/dorme
6-Muda nível de brilho
7-Próxima página da tela

Funções dos botões físicos (não consegue acordar)
Esquerdo-Jogos?
Direito-

*/
void setup(void)
{
  setCpuFrequencyMhz(10); // Começa com essa função porque os botões touch não foram inicalizados ainda
#ifdef DEBUG
  Serial.begin(115200); // serial tem que começar logo pra poder debugar
#endif
  esp_sleep_enable_timer_wakeup(60000000); //acorda após 60s
  // esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0); // desabilita o botão esquerdo

  Mutex = xSemaphoreCreateMutex();
  Serial.println();
  debug(F(__DATE__));
  debug(F(__TIME__));

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON); //mantém os GPIO ligados se estiverem
  // gpio_deep_sleep_hold_en(); //mantém reset intacto e backlight, senão apaga o display

  esp_sleep_wakeup_cause_t causa = esp_sleep_get_wakeup_cause();
  switch (causa)
  {
  case ESP_SLEEP_WAKEUP_EXT0: // Botões físicos
    Serial.print("Wakeup caused by external signal using RTC_IO");
    Setup();
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
    Serial.print("Wakeup caused by timer");
    // GiveCore0Task(); //descomentar quando passar da fase de testes
    break;
  case ESP_SLEEP_WAKEUP_TOUCHPAD: // Botões Touch
    Serial.print("Wakeup caused by touchpad");
    Setup();
    break;
  default: // Quando aperta reset ou quando recém carregou o programa
    Serial.printf("Wakeup was not caused by deep sleep: %d\n", causa);
    // GiveCore0Task(); //descomentar quando passar da fase de testes
    Setup();
    break;
  }
  SetupEverythingAboutTouchButtons();
  SetupEverythingAboutPhysicalButtons();
  touch_pad_intr_enable(); // aqui habilita a interrupção
  debug("Fim inicialização");
}

void Setup() // Setup que é rodado depois de saber o que acordou o microcontrolador
{
  debug("Setup()");
  TFTinitAndPWMDisplay();
  OnceAfterReset_TFTthings();
  timeClient.setTimeOffset(-10800); // Só precisa definir isso 1 vez a cada reset
  Core1();
  InitTimerPrintValues();
  DefineScreenPhysicalQuantities();
  debug("Fim Setup()");
}

void IRAM_ATTR TouchButton(void *arg) // system task, core0
{
  // esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TOUCHPAD); // enquanto o ESP for acordado pelo botão esquerdo, isso fica comentado
  touch_pad_intr_disable();
  uint32_t touch_pin_mask = touch_pad_get_status();
  touch_pad_clear_status();
  debug("Button " + String(touch_pin_mask) + " touched");

  switch (touch_pin_mask)
  {
  case 4: // GPIO 2
    milis = millis();
    if (milis > (general_limiter + TOUCH_DELAY))
    {
      general_limiter = millis();
    }
    break;
  case 8: // GPIO 15
    break;
  case 16: // GPIO 13
    if (millis() > general_limiter + TOUCH_DELAY)
    {
      general_limiter = millis();
      if (!BT_enabled)
      {
        // BT_enabled = BT.begin("Relogio");
        BT_enabled = true;
        debug("BT turned on");
        InitTimerBT();
        // BT.println("Inicio");
      }
    }
    break;
  case 32: // GPIO 12
    milis = millis();
    if (milis > (general_limiter + TOUCH_DELAY))
    {
      general_limiter = millis();
      if (page > 0)
      {
        page--;
        GiveCore1ShowPageWeather();
      }
    }
    break;
  case 128: // GPIO 27
    // debug("limiter1=" + String(limiter1));
    // debug("g_limiter=" + String(general_limiter));
    milis = millis();
    if (milis > (general_limiter + TOUCH_DELAY))
    {
      general_limiter = millis();
      if (page < 7)
      {
        page++;
        GiveCore1ShowPageWeather();
      }
    }
    break;
  case 256: // GPIO 33
    // debug("limiter2()=" + String(limiter2));
    milis = millis();
    if ((milis > (limiter2 + 5500)) && (milis > (general_limiter + TOUCH_DELAY)))
    {
      limiter2 = millis();
      general_limiter = limiter2;
      // debug("Button " + String(touch_pin_mask) + " touched3");
      GiveCore0Task();
    }
    break;
  case 512: // GPIO 32
    Serial.print("Touch detected on GPIO 32");
    break;
  default:
    Serial.print("Multiple touch");
    break;
  }
  debug("Fim touch");
  touch_pad_intr_enable();
}

void BotaoDireito()
{
  b++;
  if (b >= 5)
    b = 0;
  ledcWrite(pwmLedChannelTFT, backlight[b]);
  if (page != 0)
  {
    tft.fillRect(78, 216 - 2, 44, 12, TFT_BLACK); //reseta o local da indicação de brilho
    for (int i = 0; i < b + 1; i++)
      tft.fillRect(78 + 5 + (i * 7), 216 - 2, 3, 10, blue); //indicação de brilho
  }
}

void BotaoEsquerdo()
{
  // quando clicado, vai pra 0, quando solta detecta edge RISING, vem pra cá
  // esp_sleep_enable_ext0_wakeup((gpio_num_t)BOTAO_ESQUERDO, 0); // e aí não volta pra estado 0 a não ser que clique de novo
  esp_deep_sleep_start();
}

void SetFrequencyAndAdjustTimers(int freq)
{
  debug("SetFrequencyAndAdjustTimers()");
  setCpuFrequencyMhz(freq);
  InitTimerPrintValues();
  if (Timer.turnOffBT != NULL) // Se o timer já foi chamado alguma vez
  {
    if (timerStarted(Timer.turnOffBT)) // Se está iniciado
    {
      uint64_t time_BT = timerRead(Timer.turnOffBT); // Lê o valor de tempo atual
      InitTimerBT();
      timerWrite(Timer.turnOffBT, time_BT); // Escreve o valor de tempo para continuar contando
    }
  }
}

void InitTimerPrintValues()
{
  debug("InitTimerPrintValues()");
  Timer.printValues = timerBegin(0, getCpuFrequencyMhz(), true); //timer 0
  timerAttachInterrupt(Timer.printValues, &Core1, true);
  timerAlarmWrite(Timer.printValues, 1000000, true); // a cada 1 s
  timerAlarmEnable(Timer.printValues);
}

void InitTimerBT()
{
  debug("InitTimerBT()");
  if (BT_enabled)
  {
    Timer.turnOffBT = timerBegin(1, getCpuFrequencyMhz(), true); //timer 1
    timerAttachInterrupt(Timer.turnOffBT, &TurnOffBluetooth, true);
    timerAlarmWrite(Timer.turnOffBT, 1 * 60 * 1000000, false);
    timerAlarmEnable(Timer.turnOffBT);
  }
}

void Core1() //void *parameter)
{
  // while (1)
  // {
  // debug("printValues");
  /* tft.pushImage(0, 88, 135, 65, ani[frame]); //animação
      frame++;
      if (frame >= 10)
        frame = 0;
    */
  printTimeValues();
  // debug("loop 3");
  // gpio_deep_sleep_hold_en(); //volta a habilitar o gpio hold
  // }
  // vTaskDelete(NULL); //não é mais task
}

void Core0(void *parameter)
{
  getData();
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER)
    esp_deep_sleep_start();
  vTaskDelete(NULL);
}

void printTimeValues() //core1
{
  GetSystemTimeLoadedIntoVariables();
  // debug("PrintValues inicio");
  // Hora
  tft.setTextColor(deep_sky_blue, TFT_BLACK); //cor da hora
  if (strcmp(timeStamp, tt) != 0)             //só atualiza se precisar
  {
    tft.setFreeFont(&Orbitron_Light_32);
    // tft.setTextFont(6);
    // tft.setTextPadding(tft.textWidth("88:88", 6)); //precisa de fonte comum pra saber a largura do texto
    tft.setTextDatum(BC_DATUM);
    tft.fillRect(0, 0, 135, 30, TFT_BLACK);
    tft.drawString(timeStamp, TFT_WIDTH / 2 - 5, 34); //escreve hora na tela

    strcpy(tt, timeStamp);
    // tft.setTextPadding(0);
  }

  // Segundos
  if (strcmp(curSeconds, secondStamp) != 0) //só atualiza se precisar
  {
    // tft.fillRect(78, 170 + 10, 48, 28, darkred);
    // tft.setFreeFont(&Orbitron_Light_24);
    tft.setFreeFont(NULL); // importante
    tft.setTextDatum(BR_DATUM);
    tft.drawString(secondStamp, 135, 27); //escreve segundos na tela
    strcpy(curSeconds, secondStamp);
    tft.setTextDatum(0);
  }

  // Data
  tft.setTextColor(TFT_GREEN, TFT_BLACK);               //cor da data
  tft.setFreeFont(NULL);                                //importante
  tft.drawCentreString(dayStamp, TFT_WIDTH / 2, 35, 2); //valor da data
  // debug("PrintValues fim");
}

void PrintInfoTexts(text Screen)
{
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.setTextFont(2);
  tft.setCursor(4, 122);
  tft.println(Screen1.texto[0]);
  tft.setCursor(80, 122);
  tft.println(Screen1.texto[1]);
  tft.setCursor(4, 162);
  tft.println(Screen1.texto[2]);
  tft.setCursor(80, 162);
  tft.println(Screen1.texto[3]);
  tft.setCursor(4, 202);
  tft.println(Screen1.texto[4]);
  tft.setCursor(80, 205);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(1);
  tft.println("BRILHO:");
  tft.fillRect(68, 152 + 10 - 37, 1, 74 + 37, TFT_GREY); //linha de divisão no meio embaixo

  // Cidade
  if (Data.town != NULL)
  {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(2);
    tft.setTextDatum(BC_DATUM);
    // struct tm recebe struct gerada a partir de um dt em unix
    Data.struct_last_updated = *gmtime(&Data.updated_dt); // converte pra formato usual
    // char array recebe HH:MM a partir de uma struct tm
    strftime(Data.timeStamp, 6, "%H:%M", &Data.struct_last_updated);
    // tft.setCursor(0, 90);
    tft.drawString(Data.town, TFT_WIDTH / 2, 90);
    String extended_timeStamp = Data.timeStamp;
    // tm_yday é dia do ano (1-365)
    if (Data.struct_last_updated.tm_yday == DataSistema.tm_yday) //DataSistema está sempre atualzando
    {
      extended_timeStamp += "hoje";
    }
    else if (Data.struct_last_updated.tm_yday == DataSistema.tm_yday - 1) // Ontem
    {
      extended_timeStamp += "ontem";
    }
    else if (Data.struct_last_updated.tm_yday == DataSistema.tm_yday - 2) // Anteontem
    {
      extended_timeStamp += "anteontem";
    }
    else
    {
      extended_timeStamp += "muito tempo";
    }
    tft.drawString(extended_timeStamp, TFT_WIDTH / 2, 105);
  }
}

void ShowWhenWasUpdated()
{
  if (WasUpdated)
  {
    // tft.setFreeFont(&FreeSans7pt7b);
    tft.setFreeFont(&FreeSans7pt7b);
    tft.setCursor(1, 67);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print("Atualizado `");
    tft.setCursor(1, 67);
    tft.print("Atualizado as ");
    // struct tm recebe struct gerada a partir de um dt em unix
    Data.struct_last_updated = *gmtime(&Data.updated_dt); // converte pra formato usual
    // char array recebe HH:MM a partir de uma struct tm
    strftime(Data.timeStamp, 6, "%H:%M", &Data.struct_last_updated);
    tft.print(Data.timeStamp);
    // tm_yday é dia do ano (1-365)
    if (Data.struct_last_updated.tm_yday == DataSistema.tm_yday) //DataSistema está sempre atualzando
    {
      tft.print(", hoje");
    }
    else if (Data.struct_last_updated.tm_yday == DataSistema.tm_yday - 1) // Ontem
    {
      tft.print(", ontem");
    }
    else if (Data.struct_last_updated.tm_yday == DataSistema.tm_yday - 2) // Anteontem
    {
      tft.print(", anteontem");
    }
    else
    {
      // x_cursor = tft.getCursorX();
      // y_cursor = tft.getCursorY();
      tft.print("ha muito tempo");
      // tft.setCursor(x_cursor, y_cursor);
      // tft.print("h´");
    }
    tft.setFreeFont(NULL);
  }
}

void ShowPageWeather(void *pValue)
{
  debug("ShowPageWeather inicio");
  if ((!printed_page) || (WasUpdated))
  {
    // Desabilita a atualização de valores de tempo enquanto estiver escrevendo outras coisas na tela
    // Necessário para gerenciar prioridades entre timers e tasks
    timerDetachInterrupt(Timer.printValues);
    timerAlarmDisable(Timer.printValues);
    printed_page = true;
    WasUpdated = false;
    if (page == PageWeather_TempHumi)
    {
      ClearScreenForPages(55); // Limpa tela de y=55 em diante
      tft.setTextSize(1);
      // ShowWhenWasUpdated();
      PrintInfoTexts(Screen1);

      // Temperatura
      tft.setFreeFont(&Orbitron_Medium_20);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.setTextDatum(BC_DATUM);
      (Data.temperature != NULL) ? tft.drawString(Data.temperature, TFT_WIDTH_1_4, 147 + 13) : tft.drawString("?", TFT_WIDTH_1_4, 147 + 13);

      // Temperatura mínima
      (Data.min_temperature != NULL) ? tft.drawString(Data.min_temperature, TFT_WIDTH_3_4, 147 + 13) : tft.drawString("?", TFT_WIDTH_3_4, 147 + 13);

      // Sensação térmica
      (Data.feels_like != NULL) ? tft.drawString(Data.feels_like, TFT_WIDTH_1_4, 187 + 13) : tft.drawString("?", TFT_WIDTH_1_4, 187 + 13);

      // Temperatura Máxima
      (Data.max_temperature != NULL) ? tft.drawString(Data.max_temperature, TFT_WIDTH_3_4, 187 + 13) : tft.drawString("?", TFT_WIDTH_3_4, 187 + 13);

      // Umidade
      (Data.humidity != NULL) ? tft.drawString(Data.humidity + "%", TFT_WIDTH_1_4, 227 + 13) : tft.drawString("?", TFT_WIDTH_1_4, 227 + 13);
    }
    if (page == PageWeather_WindCloud)
    {
      tft.setTextSize(1);
      // ShowWhenWasUpdated();
      ClearScreenForPages(55); // Limpa tela de y=55 em diante
      PrintInfoTexts(Screen2);
      // Temperatura
      tft.setFreeFont(&Orbitron_Medium_20);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.setTextDatum(BC_DATUM);
      (Data.wind_speed != NULL) ? tft.drawString(Data.wind_speed, TFT_WIDTH_1_4, 147 + 13) : tft.drawString("?", TFT_WIDTH_1_4, 147 + 13);

      // Temperatura mínima
      (Data.wind_degree != 0) ? tft.drawString(String(Data.wind_degree), TFT_WIDTH_3_4, 147 + 13) : tft.drawString("?", TFT_WIDTH_3_4, 147 + 13);

      // Sensação térmica
      (Data.gust != NULL) ? tft.drawString(Data.gust, TFT_WIDTH_1_4, 187 + 13) : tft.drawString("?", TFT_WIDTH_1_4, 187 + 13);

      // Temperatura Máxima
      (Data.rain != NULL) ? tft.drawString(Data.rain, TFT_WIDTH_3_4, 187 + 13) : tft.drawString("?", TFT_WIDTH_3_4, 187 + 13);

      // Umidade
      (Data.cloudiness != NULL) ? tft.drawString(Data.cloudiness + "%", TFT_WIDTH_1_4, 227 + 13) : tft.drawString("?", TFT_WIDTH_1_4, 227 + 13);
    }
    PrintBrightnessBars();
    timerAttachInterrupt(Timer.printValues, &Core1, true);
    timerAlarmEnable(Timer.printValues);
  }
  vTaskDelete(NULL);
}

void getData() //core 0 executando
{
  debug("inicio getData");
  SetFrequencyAndAdjustTimers(80); //__________________________________________________________________________________________________________
  debug("inicio wifi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  debug("wifi conectando");
  TempoConexaoWifi = millis();
  while ((WiFi.status() != WL_CONNECTED) && ((millis() - TempoConexaoWifi) < 5000)) //contar o tempo aqui pra não dar crash
  {
    delay(1); // precisa pra resetar o watchdog e não dar panic
    // debug("conectando");
  }
  if ((WiFi.status() == WL_CONNECTED))
  {                      //Check the current connection status
    timeClient.update(); // Dentro da wifi porque é esse comando que realmente atualiza
    debug("http.begin");
    HTTPClient http;
    http.begin(link1 + key); //Specify the URL
    httpCode = http.GET();   //Make the request
    if (httpCode > 0)
    { //Check for the returning code
      WasUpdated = true;
      debug("http.getString");
      payload1 = http.getString();
      // Serial.println(httpCode);
      // Serial.print(String("\n" + payload));
      debug("Payload");
    }
    else
    {
      debug("Error on HTTP request1");
    }
    http.begin(link2 + key);
    httpCode = http.GET(); //Make the request
    if (httpCode > 0)
    { //Check for the returning code
      WasUpdated = true;
      debug("http.getString");
      payload2 = http.getString();
      // Serial.println(httpCode);
      // Serial.print(String("\n" + payload));
      debug("Payload");
    }
    else
    {
      debug("Error on HTTP request2");
    }
    debug("http.end");
    http.end(); //Free the resources
  }
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  debug("desligou wifi");
  SetFrequencyAndAdjustTimers(10); //__________________________________________________________________________________________________________
  debug("10MHz");
  char inp[1000];
  payload1.toCharArray(inp, 1000);
  deserializeJson(doc, inp);
  String tmp = doc["main"]["temp"];
  String hum = doc["main"]["humidity"];
  String feels_like = doc["main"]["feels_like"];
  String temp_max = doc["main"]["temp_max"];
  String temp_min = doc["main"]["temp_min"];
  String wind_speed = doc["wind"]["speed"];
  String gust = doc["wind"]["gust"];
  uint wind_degree = doc["wind"]["deg"];
  String cloudiness = doc["clouds"]["all"];
  String town = doc["name"];

  payload2.toCharArray(inp, 1000);
  deserializeJson(doc, inp);
  String rain = doc["list"]["sys"]["rain"];

  xSemaphoreTake(Mutex, 50);
  Data.feels_like = feels_like.substring(0, 4);
  Data.max_temperature = temp_max.substring(0, 4);
  Data.min_temperature = temp_min.substring(0, 4);
  Data.temperature = tmp.substring(0, 4);
  Data.wind_speed = wind_speed;
  if (wind_degree > 180)
    Data.wind_degree -= 360;
  Data.gust = gust;
  Data.humidity = hum;
  Data.cloudiness = cloudiness;
  Data.town = town;
  Data.struct_last_updated.tm_sec = Data.updated_dt;
  Data.rain = rain;
  if (WasUpdated)
  {
    Data.updated_dt = timeClient.getEpochTime();
    TempoSistema.tv_sec = Data.updated_dt;
    settimeofday(&TempoSistema, NULL);
  }
  xSemaphoreGive(Mutex);
  // debug(String(dt));
  // esp_sleep_enable_touchpad_wakeup(); // enquanto o botão esquerdo acordar o ESP, isso vai ficar comentado
}

//____________________________________________FUNÇÕES PRA ABREVIAR O CÓDIGO___________________________________________________

void SetupEverythingAboutTouchButtons()
{
  touch_pad_init();
  touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
  // Pinos à esquerda
  touch_pad_config(TOUCH_PAD_GPIO2_CHANNEL, Threshold_Touch);
  touch_pad_config(TOUCH_PAD_GPIO15_CHANNEL, Threshold_Touch);
  touch_pad_config(TOUCH_PAD_GPIO13_CHANNEL, Threshold_Touch);
  touch_pad_config(TOUCH_PAD_GPIO12_CHANNEL, Threshold_Touch);
  // Pinos à direita
  touch_pad_config(TOUCH_PAD_GPIO27_CHANNEL, Threshold_Touch);
  touch_pad_config(TOUCH_PAD_GPIO33_CHANNEL, Threshold_Touch);
  touch_pad_config(TOUCH_PAD_GPIO32_CHANNEL, Threshold_Touch);
  touch_pad_isr_register(TouchButton, NULL); //registra ISR com a função TouchButton
  touch_pad_set_meas_time((int)21000,        //Máximo 65535 - ciclos dormindo a 150 kHz - ajustar o tempo dormindo como forma de sensibilidade temporal
                          (int)3000);        //Máximo 32767 - ciclos medindo a 8 MHz - tempo suficiente para medir amostras e ter um bom resultado
  // touch_pad_intr_enable();                   // aqui habilita a interrupção
}

void SetupEverythingAboutPhysicalButtons()
{
  debug("SetupEverythingAboutPhysicalButtons()");
  pinMode(BOTAO_ESQUERDO, INPUT_PULLUP);                       // botão é pro GND
  pinMode(BOTAO_DIREITO, INPUT);                               // já tem pull up
  attachInterrupt(BOTAO_ESQUERDO, BotaoEsquerdo, RISING);      // só pode ser RISING pra funcionar
  attachInterrupt(BOTAO_DIREITO, BotaoDireito, RISING);        // só pode ser RISING pra funcionar
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BOTAO_ESQUERDO, 0); // habilita acordar com o botão esquerdo
}

void OnceAfterReset_TFTthings()
{
  if (AcabouDeIniciar)
  {
    AcabouDeIniciar = false;
    tft.setRotation(0);        // rotação definida logo
    tft.fillScreen(TFT_BLACK); // precisa pra tela não ficar com resíduo aleatório
    tft.setSwapBytes(true);    // arquitetura do processador requer que a ordem dos bytes seja invertida
    tft.setTextWrap(true);     // Wrap on width. Quebra linha
    debug("Primeiro inicio");
  }
}

void TFTinitAndPWMDisplay()
{
  debug("TFTinitAndPWMDisplay()");
  tft.init();                                          // pisca a tela se já tiver o PWM ligado
  ledcSetup(pwmLedChannelTFT, pwmFreq, pwmResolution); // inicia atributos do display
  ledcAttachPin(TFT_BL, pwmLedChannelTFT);
  ledcWrite(pwmLedChannelTFT, backlight[b]);
}

void GetSystemTimeLoadedIntoVariables()
{
  // debug("GetSystemTimeLoadedIntoVariables()");
  xSemaphoreTake(Mutex, 50);
  TempoSistema.tv_sec = time(NULL);            // busca o tempo Unix do sistema
  DataSistema = *gmtime(&TempoSistema.tv_sec); // converte pra formato usual
  strftime(dayStamp, 20, "%d/%m/%Y", &DataSistema);
  strftime(timeStamp, 6, "%H:%M", &DataSistema);
  strftime(secondStamp, 3, "%S", &DataSistema);
  xSemaphoreGive(Mutex);
  // debug(String(data.tm_mday));
  // debug(String(data.tm_mon+1));
  // debug(String(data.tm_year));
  // debug(dayStamp);
  // debug(timeStamp);
  // debug(secondStamp);
}

void TurnOffBluetooth()
{
  // BT.println("Fim");
  // BT.disconnect();
  // BT.end();
  BT_enabled = false;
  debug("BT turned off");
}

void ClearScreenForPages(byte y)
{
  tft.fillRect(0, y, 134, 239 - y, TFT_BLACK); // Limpa tela de y em diante
}

void PrintBrightnessBars()
{
  for (int i = 0; i < b + 1; i++)
    tft.fillRect(78 + 5 + (i * 7), 216, 3, 10, blue); //barras azuis de luminosidade
}

void DefineScreenPhysicalQuantities()
{
  Screen1.texto[0] = "TEMP:";
  Screen1.texto[1] = "MIN:";
  Screen1.texto[2] = "SENS.:";
  Screen1.texto[3] = "MAX:";
  Screen1.texto[4] = "UMI:";

  Screen2.texto[0] = "VENTO:";
  Screen2.texto[1] = "ANGULO:";
  Screen2.texto[2] = "RAJADA:";
  Screen2.texto[3] = "CHUVA:";
  Screen2.texto[4] = "NEBU.:";
}

void debug(String estado) //os 2 cores
{
#ifdef DEBUG_CORE0
  if (xPortGetCoreID() == 0)
  {
    if (tab)
    {
      Serial.print("\n");
    }
    String mensagem = String("CORE0: " + estado + String(" ") + String(millis()));
    Serial.print(mensagem);
    for (int i = 1; i <= (espacamento - mensagem.length()); i++)
    {
      Serial.print(" ");
    }
    // breakpoint++;
    delay(atraso);
    tab = true;
  }
#endif
#ifdef DEBUG_CORE1
  if (xPortGetCoreID() == 1)
  {
    if (!tab)
    {
      for (int i = 1; i <= espacamento; i++)
      {
        Serial.print(" ");
      }
    }
    Serial.print("CORE1: " + estado + String(" ") + String(millis()) + "\n");
    // breakpoint++;
    delay(atraso);
    tab = false;
  }
#endif
}

void GiveCore0Task()
{
  xTaskCreatePinnedToCore(
      Core0,   /* Function to implement the task */
      "Core0", /* Name of the task */
      10000,   /* Stack size in words */
      NULL,    /* Task input parameter */
      1,       /* Priority of the task */
      &Core_0, /* Task handle. */
      0);      /* Core where the task should run */
}

void GiveCore1ShowPageWeather()
{
  xTaskCreatePinnedToCore(
      ShowPageWeather,    /* Function to implement the task */
      "ShowPageWeather",  /* Name of the task */
      10000,              /* Stack size in words */
      NULL,               /* Task input parameter */
      2,                  /* Priority of the task */
      &ShowPageWeather_1, /* Task handle. */
      1);                 /* Core where the task should run */
}

void loop() {}

// Outras informações

/* JSON (payload)
  {"coord":{"lon":-51.5036,"lat":-29.2975},
  "weather":[{"id":802,"main":"Clouds","description":"scattered clouds","icon":"03d"}],
  "base":"stations",
  "main":{"temp":21.37,"feels_like":21.85,"temp_min":20,"temp_max":24.44,"pressure":1009,"humidity":61},
  "visibility":10000,
  "wind":{"speed":0.89,"deg":270,"gust":8.05},
  "clouds":{"all":29},
  "dt":1612546015,
  "sys":{"type":3,"id":2036789,"country":"BR","sunrise":1612515567,"sunset":1612563639},
  "timezone":-10800,
  "id":3466933,
  "name":"Carlos Barbosa",
  "cod":200}
  */