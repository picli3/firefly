# Mediciones tomada con las 2 luces encendidas 


## Medicion con el modulo de corriente

Esta medicion se hizo usando el modulo de medicion de corriente

V=6267 mV I=0 mA T=-24.07 C AC=1 SoH=95%
V=130 mV I=0 mA T=71.81 C AC=1 SoH=0%
V=5571 mV I=29 mA T=-327.68 C AC=1 SoH=8%
V=5536 mV I=27 mA T=-21.94 C AC=1 SoH=4%
V=5495 mV I=27 mA T=-327.68 C AC=1 SoH=0%

## Medicion con multimetro

LA medicion con el multimetro dio 1060 mA, es como que el modulo INA esta limitado.

## Interpretacion

El resistor del modulo esta marcado `R010`, por lo que su valor es 10 mOhm. El
firmware usado en estas mediciones estaba calibrado para 50 mOhm y escribia 1024
en el registro de calibracion. Para `R010` y un Current LSB de 100 uA, el valor
correcto es 5120. Por esta razon, las lecturas de 27-29 mA estaban subestimadas
por un factor de cinco y representan aproximadamente 135-145 mA bajo las mismas
condiciones de esa prueba.

Los 1060 mA se midieron reemplazando el INA226, no simultaneamente en serie. Eso
confirma una diferencia entre ambas configuraciones, pero no demuestra que el
chip INA226 limite la corriente. A 1060 mA, un shunt de 10 mOhm solo debe caer
10.6 mV y disipar aproximadamente 11 mW.

Para localizar la caida, conectar el INA226 y el multimetro simultaneamente en
serie y medir tambien la tension directamente entre `IN+` e `IN-`. Si la caida
del shunt es cercana a 10.6 mV pero existe una caida mucho mayor entre bateria y
lampara, revisar terminales, soldaduras, pistas y cables. La corriente de carga
no debe pasar por `VCC`, GND ni por cables Dupont de senal.

## luego del ajuste

Luego de cambira la RS a 5120 la corrienre medida es esta:

V=6000 mV **I=98 mA** T=-327.68 C AC=1 SoH=62%

38
