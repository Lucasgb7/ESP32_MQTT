# _ESP32 e MQTT_

Esse repositório apresenta uma aplicação utilizando o microcontrolador [ESP32 DEV KIT v1](https://randomnerdtutorials.com/getting-started-with-esp32/) com framework ESP-IDF baseando o código em FreeRTOS. O objetivo é utilizar o ESP32 para adquirir dados de sensores analógicos e de algum periférico que utilize um protocolo de comunicação. Os dados são estruturados em um JSON e enviados via WiFi para um broker MQTT.

## Instalação

- Baixar e instalar o [Visual Studio Code](https://code.visualstudio.com/download).
- Instalar a extensão da Espressif ([ESP-IDF](https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/tutorial/install.md)).
- ```git clone https://github.com/Lucasgb7/ESP32_MQTT.git ```

## Funcionamento

O código em funcionamento é originado do exemplo fornecido pela [Espressif de MQTT TCP](https://github.com/espressif/esp-idf/blob/36f49f361c001b49c538364056bc5d2d04c6f321/examples/protocols/mqtt/tcp/main/app_main.c) o qual foi modificado para tal propósito. 

Inicialmente o código começa com declarações de bibliotecas, e variáveis globais para uso do MQTT e dos GPIos.
```
#define BLINK_GPIO GPIO_NUM_2

static const char *TAG = "MOSQUITTO";
static uint8_t s_led_state = 0;
static const TickType_t oneSecond = pdMS_TO_TICKS(1000);
static esp_adc_cal_characteristics_t adc1_chars;
```
Na função principal _app_main()_ são realizados impressões na tela de informações sobre o ESP32 e o carregamento do código dentro dele. Até então chegar na função _app_start()_.

Na função _app_start()_ são realizadas as configurações:
- Potenciometro: Conectado no ADC01_CHANEL 0 (GPIO36), foi atenuado numa banda de 11dB para ampliar a amostragem de tensão. 
- LED: Definido o pino de LED (2) como saída.
- MQTT: Configurações de conexão na rede WiFi são definidas, e também o IP para hostear o broker. Todas essas configurações são ocultas no arquivo __sdkconfig__.

Na configuração do MQTT é chamado a função que gerancia os eventos ocasionados, passando o cliente recém criado como argumento.

### Função 'mqtt_event_handler'

Essa função funciona como um gerenciador de eventos do MQTT. Toda vez que um evento ocorre, passa por essa função a qual identifica que tipo de evento e o que vai fazer de acordo com cada valor. Basicamente existe um _switch_ que identifica o evento, e dentro de cada _case_ existe algo específico. 

#### Envio de dados no MQTT

Quando um evento é do tipo "MQTT_EVENT_CONNECTED", significa que o cliente está conectado ao broker, isso é, está pronto para receber e enviar mensagens. 

Dentro desse case, é realizado a leitura do potenciometro (GPIO36) e inserido em um pacote JSON. Assim como uma leitura do Serial Monitor que também é passada como JSON. No final, esses dados são publicados no tópico __"esp32/output/"__. 

#### Recebimento de dados no MQTT

No mesmo caso de MQTT_EVENT_CONNECTED, é realizado uma inscrição no tópico __"esp32/input/"__. Assim que ocorre um evento que esteja recebendo um dado, é disparado o evento "MQTT_EVENT_DATA". 

Nesse caso, é realizada uma conversão nos valores que chegam (tópico e dado) para string. A string de tópico é comparada com a string inscrita (_"esp32/input/"_), e caso seja nesse tópico que o dado tenha chego passa para próxima etapa.

```
// Verifica se a mensagem é de mudança de LED
if (strcmp(data_str, "LED") == 0) {
    ESP_LOGI("/esp32/input/", "Alterou o estado do LED");
    gpio_set_level(BLINK_GPIO, s_led_state);
    s_led_state = !s_led_state;
    vTaskDelay(oneSecond * 3);
// Ou se foi mensagem de SOS
} else if (strcmp(data_str, "SOS") == 0) {
    ESP_LOGI("/esp32/input/", "ALERTA - SOS");
    sos_flash(); 
}
```
Nessa etapa, ele pisca o LED do microcontrolador conforme a mensagem passada pelo usuário.

## Problemas e Melhorias

- A estruturação do código não seguiu uma forma padronizada de codificação. Isso se torna devido a documentação da Espressif não ser muito didática não possuir tanto fomento na comunidade. Muitas vezes fiquei recioso de alterar alguma coisa por não entender o funcioamento.

- A utilização do FreeRTOS poderia ter ajudado na paralelização de tasks por exemplo para sempre publicar os dados e mesmo assim ficar "escutando" algum dado chegando num tópico inscrito.