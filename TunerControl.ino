// Программа управления тюнером R8CT
// ra9qat@mail.ru 
// версия 2017-04-24

// Для отладки подключите терминал на скорости 115200 bps

#include <EEPROM.h>
#include <Servo.h>

#define PortSpeed 115200   // скорость подключения терминальной программы (на время отладки)

#define LED 13             // номер порта для подключения светодиода
#define KeysPin 0          // номер аналогово порта для подключения клавиш (5 аналог он же 19 цифр)

#define ServoPin0 10        // номер вывода для подключения сервопривода галетника
#define ServoPin1 11       // номер вывода для подключения сервопривода конденсатора 1
#define ServoPin2 9       // номер вывода для подключения сервопривода конденсатора 2

#define MinVarCap 1        // начальный угол сервопривода конденсаторов
#define MaxVarCap 170      // конечный угол сервопривода конденсаторов
#define StepVarCap 5      // шаг изменения угла сервопривода конденсаторов

#define DetenSelector 1    // угол ослабления натяга сервопривода галетника

#define ServoSlow 10       // Задержка (мс) при повороте сервопривода на 1 градус

#define KeyDist 25         // максимально допустимое отклонение значения при нажатии клавиши подключенной к аналоговому порту +- это значение
#define NumKeys 4          // количество клавиш подключенных к аналоговому порту
#define LongPressTime 3000 // минимальная продолжительность длинного нажатия клавиш (количество циклов. длина цикла зависит от сложности программы)
#define ShortPressTime 200 // минимальная продолжительность длинного нажатия клавиш (количество циклов. длина цикла зависит от сложности программы)

#define Key1minus 1        // номер клавиши для уменьшения угла серво 1 (от 1 до 4,  в пордяке значений в массиве KeyValue[])
#define Key1plus  2        // серво 1 ++
#define Key2minus 3        // серво 2 --
#define Key2plus  4        // серво 2 ++

#define NumBands 4        // количество диапазонов
#define BandBit 0          // каким уровнем декодер диапазонов кодирует выбранный дипазон 0 или 1
#define BandAdr 0          // адрес ПЗУ после которого хранятся данные по диапазонам

#define MinSel 1        // начальный угол сервопривода галетника
#define MaxSel 180      // конечный угол сервопривода галетника
#define StepSelSmall 1  // малый шаг изменения угла сервопривода галетника
#define StepSelLarge 5  // большой шаг изменения угла сервопривода галетника

boolean TuningON;           // признак режима настройки тюнера
unsigned long TimesPressed; // продолжительность нажатия клавиши (в циклах)
unsigned long TimesCounter; // вспомогательная переменная для подсчета циклов нажатия клавиши
float prev_value;           // вспомогательная переменная предыдущее значение АЦП

word KeyValue[NumKeys] = {925, 847, 774, 690}; // значения АЦП при нажатии клавиш подключенных к аналоговому порту
word KeyPressed;            // 1,2.... значение нажатой клавиши до отпускания, после отпускания KeySignal=0

byte BandPin[NumBands] = {16,17,18,19}; // номера входов на которые подаётся сигнал от декодера диапазонов
                                        // цифровые 0-13, аналоговые 14-19 
                                        // Заняты: 13 цифр. - светодиод, 14 аналоговый - клавиши
                                        //         9,10,11 цифр. - сервоприводы 
                                        //         1 TX терминал, бывает ложное срабатвание при печати в терминал

byte Band; // номер текущего диапазона - номер входа в массиве BandPin, начиная с 0 по NumBands-1

int DetenDir; // Направление ослабления сервопривода галетника + или -

byte pServo0; // Абс. значение угла для сервопривода галетника
byte pServo1; // Абс. значение угла для сервопривода конденсатора 1
byte pServo2; // Абс. значение угла для сервопривода конденсатора 2

Servo Servo0; // Переменная для работы с сервоприводом галетника
Servo Servo1; // Переменная для работы с сервоприводом конденсатора 1
Servo Servo2; // Переменная для работы с сервоприводом конденсатора 2

boolean ServoAttached; // Признак того, было ли выполено подключение к сервоприводам или ещё нет

boolean TuningSEL;      // признак режима настройки галетника


// Отладочная функция определения значения АЦП нажатой клавиши, вставить её в основной цикл, всё остальное в нём закомментировать

void TestKey (void) 
{
  float value;
  value = analogRead(KeysPin);
  Serial.print(value);
  Serial.print("\n");
  delay(300);
}

// Функция чтения кода клавиши
// Возвращает true когда клавиша была отпущена
// В глобальных переменных KeyPressed - номер клавиши, TimesPressed - продолжительность нажатия

boolean GetKey (void) 
{ 
  float value;
  value = analogRead(KeysPin);
  if (value > KeyDist) // если значение больше минимального порогового
  {
    TimesCounter++;         // подумать насчёт индикации
    prev_value=value;
  }
  else
  {
    if (prev_value > KeyDist)
    {
      byte k;
      for(k = 0; k < NumKeys; k++) 
      if (prev_value > KeyValue[k]-KeyDist && prev_value < KeyValue[k]+KeyDist) 
        KeyPressed=k+1;     
      TimesPressed=TimesCounter;   
      TimesCounter=0;
      prev_value=0;
      return true;  
    }
  }  
  return false;  
}

// Анализ входов подключенных к декодеру диапазонов
// Возвращает true если состояние сигнала на выводах изменилось
// Если не изменялось или ни на одном из выводов нет сигнала - false
// В переменной Band будет номер диапазона

boolean BandChanged(void)
{
  byte b;
  byte NewBand;
  NewBand=Band;

  for (b=0; b < NumBands; b++)
    {
      if (digitalRead(BandPin[b])==BandBit)
       {NewBand=b;}
    } 
      
  if (NewBand == Band)
    {return false;}
  else  
    {
      if (EEPROM.read(4*Band+3) < EEPROM.read(4*NewBand+3)) // если угол поворота галетника в сторону увеличения, то ослаблять натяг в противоположную
       {DetenDir=-1;}
     else 
       {DetenDir=1;} 
     Band=NewBand; 
     return true;
    }
}

// Чтение настроек тюнера из ПЗУ для указанного в переменной Band диапазона
// в переменные pServo... В ПЗУ хранятся парами, начиная с адреса 1
// Если в ячейке ещё ничего не сохранялось, то значения позиций серво не меняются
void ROMread(void)
{ 
  byte p;
  if (Band < NumBands)
  {
    p=EEPROM.read(4*Band+1);
    if (p < 255) {pServo1=p;}
    p=EEPROM.read(4*Band+2);
    if (p < 255) {pServo2=p;}
    p=EEPROM.read(4*Band+3);
    if (p < 255) {pServo0=p;}
  }  
}

// Запись настроек тюнера из ПЗУ для указанного в переменной Band диапазона
// Из переменных pServo... В ПЗУ хранятся парами, начиная с адреса 1
void ROMwrite(void)
{
  EEPROM.write(4*Band+1, pServo1);
  EEPROM.write(4*Band+2, pServo2);  
  EEPROM.write(4*Band+3, pServo0);  
}

// Медленный поворот сервопривода
// Параметры: сервопривод, угол

void SlowMove(Servo Srv, byte nA, byte ServoPin)
{
  byte n;
  byte s;
  Srv.attach(ServoPin);
  n=Srv.read();
  if (n < nA)
    {s=1;}
  else
    {s=-1;}  
  while (n != nA)
  {
    n=n+s;
    delay(ServoSlow);
    Srv.write(n);
  }
  Srv.detach();  
}

// Настройка угла конденсаторов

void TuneVarCaps()
{
 
         // Серво 1 --
        if (KeyPressed==Key1minus)
        {
          if ((pServo1-StepVarCap) >= MinVarCap)
          {
            pServo1=pServo1-StepVarCap;
            Servo1.write(pServo1);
          }
        }
        // Серво 1 ++
        if (KeyPressed==Key1plus)
        {
          if ((pServo1+StepVarCap) <= MaxVarCap)
          {
            pServo1=pServo1+StepVarCap;
            Servo1.write(pServo1);
          }
        }
        // Серво 2 --
        if (KeyPressed==Key2minus)
        {
          if ((pServo2-StepVarCap) >= MinVarCap)
          {
            pServo2=pServo2-StepVarCap;
            Servo2.write(pServo2);
          }
        }
        // Серво 2 ++
        if (KeyPressed==Key2plus)
        {
          if ((pServo2+StepVarCap) <= MaxVarCap)
          {
            pServo2=pServo2+StepVarCap;
            Servo2.write(pServo2);
          }
        }

        Serial.print(" Servo1 = ");
        Serial.print(pServo1);
        Serial.print("   Servo2 = ");
        Serial.print(pServo2);
        Serial.print("\n");
  
}

// Настройка угла галетника

void TuneSelector()
{

          // большой шаг --
        if (KeyPressed==Key1minus)
        {
          if ((pServo0-StepSelLarge) >= MinSel)
          {
            pServo0=pServo0-StepSelLarge;
            Servo0.write(pServo0);
          }
        }
        // большой шаг ++
        if (KeyPressed==Key1plus)
        {
          if ((pServo0+StepSelLarge) <= MaxSel)
          {
            pServo0=pServo0+StepSelLarge;
            Servo0.write(pServo0);
          }
        }
        // малый шаг --
        if (KeyPressed==Key2minus)
        {
          if ((pServo0-StepSelSmall) >= MinSel)
          {
            pServo0=pServo0-StepSelSmall;
            Servo0.write(pServo0);
          }
        }
        // малый шаг ++
        if (KeyPressed==Key2plus)
        {
          if ((pServo0+StepSelSmall) <= MaxSel)
          {
            pServo0=pServo0+StepSelSmall;
            Servo0.write(pServo0);
          }
        }

        Serial.print("Servo0 = ");
        Serial.print(pServo0);
        Serial.print("\n");
  
}

// Выключение режима настройки конденсаторов, сохранение в ПЗУ

void TuningOFF()
{
          TuningON=false;
          Servo1.detach();
          Servo2.detach(); 
          Serial.print("Tuning Capacitors OFF. Saving data...\n");
          ROMwrite();
          Serial.print("OK\n");
          digitalWrite(LED, LOW);

}


// Выключение режима настройки галетника, сохранение в ПЗУ

void TuningSelOFF()
{
          TuningSEL=false;
          Servo0.detach();
          Serial.print("Tuning Selector OFF. Saving data...\n");
          ROMwrite();
          Serial.print("OK\n");
          digitalWrite(LED, LOW);

}
// -----------------------------------------------------------------------------------------------

void setup() 
{

  pinMode(LED, OUTPUT);
    
  Serial.begin(PortSpeed);
  Serial.print("Connected!");
  Serial.print("\n");
  
  Band=NumBands;

// подтяжка тестовых выводов бенд декодера для отладки
// состояние выводов для отладки
  byte k;
    for (k=0; k < NumBands; k++)
    {
      digitalWrite(BandPin[k], HIGH);
      Serial.print("Band Pin ");
      Serial.print(k);
      Serial.print(" = ");
      Serial.print(digitalRead(BandPin[k]));
      Serial.print("\n");
    }

   pServo0=0;
   pServo1=0;
   pServo2=0;
   
   ServoAttached=false;
   TuningON=false;
   TuningSEL=false;
 
};

// -----------------------------------------------------------------------------------------------

void loop () 
{
  // Для программирования кодов кнопок раскомментировать функцию TestKey(), загрузить скетч в Ардуино, подключиться к терминалу, 
  // нажимать кнопки, смотреть коды, записать их в массив KeyValue[NumKeys] в начале скетча
  // Какая клавиша за что отвечает задано в блоке дефайн в начале скетча, т.е. первая по счету это уменьшение угла сервы 1, 
  // вторая увеличение и т.д. Эти номера можно менять по своему усмотрению.
  // #define Key1minus 1        // номер клавиши для уменьшения угла серво 1 (от 1 до 4,  в пордяке значений в массиве KeyValue[])
  // #define Key1plus  2        // серво 1 ++
  // #define Key2minus 3        // серво 2 --
  // #define Key2plus  4        // серво 2 ++

  //TestKey(); 

  // обработка нажатий клавиш
  if (GetKey())
  {
    // короткое нажатие
    if (TimesPressed > ShortPressTime && TimesPressed < LongPressTime)
    {
      Serial.print(KeyPressed);
      Serial.print(" S");
      Serial.print("\n");
      
      // Настройка конденсаторов
      if (TuningON)
      {
        TuneVarCaps();
      }
      
      // Настройка галетника
      if (TuningSEL)
      {
         TuneSelector();
      }
    } // короткое нажатие
    
    
    // длинное нажатие
    if (TimesPressed >= LongPressTime)
    {
      // Настройка конденсаторов    
      if ((KeyPressed==Key1minus)||(KeyPressed==Key1plus))
      {
        if (!TuningON)
        {
          if (TuningSEL) {TuningSelOFF();} // Если была включена настройка галетника, выключаем
          TuningON=true;
          Servo1.attach(ServoPin1);
          Servo2.attach(ServoPin2); 
          Serial.print("Tuning Capacitors ON\n");
          digitalWrite(LED, HIGH);
        }
        else
        {
          TuningOFF();
        }        
      }
      
      // Настройка галетника
      if ((KeyPressed==Key2minus)||(KeyPressed==Key2plus))
      {
        if (!TuningSEL)
        {
          if (TuningON) {TuningOFF();}  // Если была включена настройка конденсаторов, выключаем
          TuningSEL=true;
          Servo0.attach(ServoPin0);
          Serial.print("Tuning Selector ON\n");
          digitalWrite(LED, HIGH);
        }
        else
        {
          TuningSelOFF();
        }        
      }

    } // Длинное нажатие

 } // Обработка нажатий клавишь
  
 
  if (BandChanged())
    {
      ROMread(); 

      if (Band < NumBands)
      {
        Serial.print("Band Pin - ");
        Serial.print(Band);
        Serial.print("\n");

        // установить сервы
   
        SlowMove(Servo0, pServo0, ServoPin0);        
        
        Serial.print("Selector angle set - ");
        Serial.print(pServo0);
        Serial.print("\n");
/*
        Servo0.write(pServo0+DetenSelector*DetenDir); // ослабление галетника
        Serial.print("Angle deten - ");
        Serial.print(pServo0+DetenSelector*DetenDir);
        Serial.print("\n");
*/
        SlowMove(Servo1, pServo1, ServoPin1);        
        SlowMove(Servo2, pServo2, ServoPin2);

      }
    }
 
};
