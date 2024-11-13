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

При это исходное значение источника питания, при вычисленном значении поступающего на контрольный пин информации, будет обратной функцией:
$$ V_{in} = V_{out} * \frac{(R1 + R2)}{R2} $$

Значение, возвращаемое АЦП, работающим в 12-битном режиме, будет находиться в диапазоне от 0 до 4095 ([из документации](https://demo-dijiudu.readthedocs.io/en/stable/api-reference/peripherals/adc.html#_CPPv225adc1_config_channel_atten14adc1_channel_t11adc_atten_t)). Затем мы можем преобразовать напряжение в значение АЦП, используя следующую формулу:
$$ V_{adc} = \frac{(ADC * ADC\_REFERENCE)}{ADC\_RESOLUTION} $$

где:  
***ADC*** - значение на пине  
***ADC_REFERENCE*** = 1.1 В - референсное (опорное) напряжение на пине  
***ADC_RESOLUTION*** = 4096 - максимальное значение на порту  

Отсюда реальное значение напряжения:
$$ V = ADC * \frac{ADC\_REFERENCE}{ADC\_RESOLUTION} * \frac{R2}{(R1 + R2)} $$
  
```C++
#define ADC_REFERENCE 1.1
#define ADC_RESOLUTION 4096

float voltage(int adc)
{
    return (float)adc * (ADC_REFERENCE / ADC_RESOLUTION) * ((R1 + R2) / R2);
}
```

Так как в жизни мы привыкли видеть заряд батареи в %. То и код, который считывает значение с АЦП и преобразует его в уровень заряда батареи, напишу с его преобразованием значсения в диапазоне от 0 до 100%. В качестве приближенной оценки приму напряжение батареи 4.2 В за 100%, а напряжение 3.3 В за 0%. Я переведу их в милливольты, чтобы избежать вычислений с плавающей точкой.
```C++
#define VOLTAGE_MAX 4.2
#define VOLTAGE_MIN 3.3

int calc_battery_percentage(float vadc)
{
    int battery_percentage = 100 * (vadc - VOLTAGE_MIN) / (VOLTAGE_MAX - VOLTAGE_MIN);

    if (battery_percentage < 0)
        battery_percentage = 0;
    if (battery_percentage > 100)
        battery_percentage = 100;

    return battery_percentage;
}
```

Итак, минимальное и максимальное значения напряжения батареи, преобразованные резистивным делителем и возвращаемые АЦП, составляют:
- min: 2457 - соответствует напряжению 3.3 В
- max: 3130 - соответствует напряжению 4.2 В


Для работы кода потребуется определить переменную кода пина, на котором будет определяться напряжение:
```C++
#define ANALOG_IN_PIN  32                                                   // ESP32 pin GPIO32 (ADC1_4) connected to voltage sensor
```

## Код определения напряжения  
В целом программный код определения напряжения я разобъю на 3 раздела:
1. flix.h
2. flix.ino  
3. battery.ino
4. cli.ino

### 1. flix.h
Здесь необходимо объявить все функции, которые понадобятся для определения напряжения. По сути это 2 функции:  
```C++
// battery
void battery_control();                                                     // показать процент заряда аккумулятора
uint16_t analogRead(uint8_t pin);                                           // чтение аналогово выхода из Arduino.h
```

### 2. flix.ino  
Устанавливаю затухание АЦП на 11 дБ (до ~3,3 В на входе).
В раздел setup() надо добавить:
```C++
// battery: set the ADC attenuation to 11 dB (up to ~3.3V input)
analogSetAttenuation(ADC_11db);
```

### 3. battery.ino  
Создаю новый модуль *battery.ino*.  
В данном модуле содержаться параметры и функции вычисления напряжения.

Код модуля **battery.ino**
```C++
#define ANALOG_IN_PIN  32                                                   // ESP32 pin GPIO32 (ADC1_4) connected to voltage sensor
#define R1 47                                                               // resistor values in voltage sensor (in kiloohms)
#define R2 10                                                               // resistor values in voltage sensor (in kiloohms)
#define ADC_RESOLUTION 4096                                                 // range ADC working in 12-bit mode
#define VOLTAGE_MAX 4.2                                                     // max battery voltage
#define VOLTAGE_MIN 3.3                                                     // min battery voltage
#define ADC_REFERENCE 1.1                                                   // reference voltage of ESP32
#define BATTERY_MIN_PERCENTAGE 15                                           // min battery percentage to warning

// calc voltage
float voltage(int adc)
{
    return (float)adc * (ADC_REFERENCE / ADC_RESOLUTION) * ((R1 + R2) / R2);
}

// calc percentage of battery
float calc_battery_percentage(float vadc)
{
    float battery_percentage = 100.0 * (vadc - VOLTAGE_MIN) / (VOLTAGE_MAX - VOLTAGE_MIN);

    if (battery_percentage < 0.0)
        battery_percentage = 0.0;
    if (battery_percentage > 100.0)
        battery_percentage = 100.0;

    return battery_percentage;
}

// show percentage of battery
void battery_control() {
    // int adc = analogRead(ANALOG_IN_PIN);
    int adc = 2177;

    bat = calc_battery_percentage(voltage(adc));
	Serial.printf("Battery: %.2f%\n", bat);
	// print a low battery warning
    if (bat <= 15.0)
        Serial.print("Warning! Battery is low.\n");
}
```

В модуле по сути раздел объявления переменных, и 3 функции:
- ***voltage(int adc)*** - расчет напряжения по данным с контрольного PIN;
- ***calc_battery_percentage(float vadc)*** - расчет процента зарядааккумулятора, учитывая максимальное и минимальное возможные значения напряжения аккумулятора;
- ***battery_control()*** - функция, вызываемая в командной строке, и показывающая заряд аккумулятора.

### 4. cli.ino
В данном модуле организовано общение с коптером, через использование командной строки.  
Для получения информации о состоянии батареи необходимо указать команду **bat**.  
В код необходимо добавить обработку данной команды и вызов функции battery_control(), которую для видимости в *cli.ino* и прописывал в *flix.h*.
```C++
    ...
    } else if (command == "reset") {
		attitude = Quaternion();
    // - * - insert - * -
	} else if (command == "bat") {
		battery_control();
    // - * - end insert - * -
	} else {
    ...
```