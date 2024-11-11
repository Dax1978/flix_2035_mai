# Контроль заряда батареи коптера Flix
*Поток 3, Горбунов Евгений*

В качестве выпускной работы по обучению на программе ***"Разработка ПО для полетного контроллера БАС"*** мною была выбрана тема ***"Модификация прошивки Flix в части контроля заряда аккумулятора и реакции на достижение предельного значения разряда"***  

## Краткое описание проекта Flix  
В 2019-м году, [Олег Калачев](https://habr.com/ru/users/chv/), взялся за достаточно амбициозную задачу — создать квадрокоптер, не используя готовую плату полетного контроллера и готовую прошивку, то есть реализовать вообще все с нуля. И за 4 года он реализовал свою идею, и при этом сделал свой [проект](https://github.com/okalachev/flix) открытым!  
В результате был разработан дрон, схема которого представлена на рис.1:
![Схема дрона Flix](./img/Схема%201.png "Рис. 1 Схема дрона Flix")  
Рис. 1 Схема дрона Flix  

## Определение напряжения на плате STM32  
Для питания квадрокоптера используется однобаночный литий-полимерный аккумулятор с номинальным напряжением 3.7 В. В данном случае используется аккумулятор с высокой токоотдачей, чтобы тока хватило и на моторы, и на микроконтроллер с Wi-Fi, IMU и RC-приемник. Непосредственно микроконтроллер ESP32 питается от 3.3 В, но в выбранных платах ESP32 Mini и GY-91 есть встроенные LDO-регуляторы напряжения, которые позволили запитать их напрямую от аккумулятора.  
Однако для контроля напряжения имеется соответствующая схема, выводящая напряжение с резистивного делителя на пин 32. При этом сам резистивный делитель реализован на 2-х резисторах, номиналом 47К и 10К, которые можно сразу будет записать:
```C++
#define R1 47
#define R2 10
```

Соответственно выходное напряжение резистивного делителя будет рассчитываться как:
$$ V_{out} = V_{in} * \frac{R2}{(R1 + R2)} $$

(или все таки ???)
$$ V_{out} = V_{in} * \frac{(R1 + R2)}{R2} $$

Что можно также определить в функцию:
```C++
#define VOLTAGE_OUT(Vin) (Vin * (R2 / (R1 + R2)))

(или все таки ???)
#define VOLTAGE_OUT(Vin) (Vin * ((R1 + R2) / R2))
```

Так как в жизни мы привыкли видеть заряд батареи в %. То и код, который считывает значение с АЦП и преобразует его в уровень заряда батареи, напишу с его преобразованием значсения в диапазоне от 0 до 100%. В качестве приближенной оценки приму напряжение батареи 4.2 В за 100%, а напряжение 3.3 В за 0%. Я переведу их в милливольты, чтобы избежать вычислений с плавающей точкой.
```C++
#define VOLTAGE_MAX 4200
#define VOLTAGE_MIN 3300
```

Далее учитывая конструктивные особенности выбранного микроконтроллера, а именно что опорное напряжение АЦП ESP32 составляет 1100 мВ, определю его в коде:  
```C++
#define ADC_REFERENCE 1100
```

Значение, возвращаемое АЦП, работающим в 12-битном режиме, будет находиться в диапазоне от 0 до 4095 ([из документации](https://translated.turbopages.org/proxy_u/en-ru.ru.1954bb9a-6731dd5b-ab955629-74722d776562/https/demo-dijiudu.readthedocs.io/en/stable/api-reference/peripherals/adc.html#_CPPv225adc1_config_channel_atten14adc1_channel_t11adc_atten_t)). Затем мы можем преобразовать напряжение в значение АЦП, используя следующую формулу:
```C++
#define VOLTAGE_TO_ADC(in) ((ADC_REFERENCE * (in)) / 4096)
```

Итак, минимальное и максимальное значения напряжения батареи, преобразованные резистивным делителем и возвращаемые АЦП, составляют:  
```C++
#define BATTERY_MAX_ADC VOLTAGE_TO_ADC(VOLTAGE_OUT(VOLTAGE_MAX))
#define BATTERY_MIN_ADC VOLTAGE_TO_ADC(VOLTAGE_OUT(VOLTAGE_MIN))
```

Извлечение значения из АЦП довольно простое. Допустим, у меня есть значение из АЦП в переменной с именем adc. Тогда расчет уровня заряда батареи будет выглядеть следующим образом:
```C++
int calc_battery_percentage(int adc)
{
    int battery_percentage = 100 * (adc - BATTERY_MIN_ADC) / (BATTERY_MAX_ADC - BATTERY_MIN_ADC);

    if (battery_percentage < 0)
        battery_percentage = 0;
    if (battery_percentage > 100)
        battery_percentage = 100;

    return battery_percentage;
}
```

Для работы кода потребуется определить переменную кода пина, на котором будет определяться напряжение:
```C++
#define ANALOG_IN_PIN  32                                                   // ESP32 pin GPIO32 (ADC1_4) connected to voltage sensor
```

## Код определения напряжения  
В целом программный код определения напряжения я разобъю на 3 раздела:
1. Функция для раздела инициализации, вызываемая в **setup()** при инициализации коптера;
2. Функции для работы системы контроля заряда аккумулятора, вызываемая постоянно в **loop()**.

### 1. Функция для раздела инициализации  
В данной функции будут определены все настройки, необходимые в последующем для постоянного вычисления напряжения в цикле.

```C++
// Функция инициализации параметров системы контроля заряда батареи
void battery_control_setup()
{
    // Настройку порта делать нецелесообразно, т.к. в основном коде данная настройка уже присутствует и составляет: #define SERIAL_BAUDRATE 115200
    // Serial.begin(9600);
    // set the ADC attenuation to 11 dB (up to ~3.3V input)
    // устанавливаю затухание АЦП на 11 дБ (до ~3,3 В на входе)
    analogSetAttenuation(ADC_11db);
}
```

### 2. Функции для работы системы контроля заряда аккумулятора  
В данном разделе описан код модуля контроля заряда аккумулятора и его вызываемая функция в *loop()*

Код модуля **battery.ino**
```C++
#define ANALOG_IN_PIN  32                                                   // ESP32 pin GPIO32 (ADC1_4) connected to voltage sensor
#define R1 47                                                               // resistor values in voltage sensor (in kiloohms)
#define R2 10                                                               // resistor values in voltage sensor (in kiloohms)
#define ADC_RESOLUTION 4096                                                 // range ADC working in 12-bit mode
#define VOLTAGE_MAX 4200                                                    // max battery voltage
#define VOLTAGE_MIN 3300                                                    // min battery voltage
#define ADC_REFERENCE 1100                                                  // reference voltage of ESP32
#define BATTERY_MIN_PERCENTAGE 15                                           // min battery percentage to warning

#define VOLTAGE_OUT(Vin) (Vin * ((R1 + R2) / R2))                           // calc output voltage of the resistor divider
#define VOLTAGE_TO_ADC(in) ((ADC_REFERENCE * (in)) / BC_ADC_RESOLUTION)     // calc determine voltage at adc input
#define BATTERY_MAX_ADC VOLTAGE_TO_ADC(VOLTAGE_OUT(VOLTAGE_MAX))            // calc voltage max
#define BATTERY_MIN_ADC VOLTAGE_TO_ADC(VOLTAGE_OUT(VOLTAGE_MIN))            // calc voltage min

// Определение процента заряда аккумулятора
float calc_battery_percentage(float adc)
{
    float battery_percentage = 100 * (adc - BATTERY_MIN_ADC) / (BATTERY_MAX_ADC - BATTERY_MIN_ADC);

    if (battery_percentage < 0)
        battery_percentage = 0;
    if (battery_percentage > 100)
        battery_percentage = 100;

    return battery_percentage;
}

void battery_control()
{    
    int adc_value = analogRead(ANALOG_IN_PIN);                              // read the analog input    
    float voltage_adc = VOLTAGE_TO_ADC(adc_value);                          // determine voltage at adc input
    float voltage_in = VOLTAGE_OUT(voltage_adc);                            // calculate voltage at the sensor input
    float battery_percentage = calc_battery_percentage(voltage_in)          // calculate battery percentage

    // Что будем делать с этим:

    // print results to serial monitor to 2 decimal places
    Serial.print("Battery: ");
    Serial.println(battery_percentage, 2);

    // print a low battery warning
    if (battery_percentage <= BATTERY_MIN_PERCENTAGE)
    {
        Serial.print("Warning! Battery is low.");
    }
}
```

Добавить в **flix.ino** после *parseInput();*:
```C++
...
battery_control();
...
```