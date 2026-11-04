# ЛР5 — Вариант 21. Сетевая служба сжатия данных (RLE)

Протокол: TCP  
STUDENT_ID: 90  
Порт по умолчанию: 20090

## Сборка
Visual Studio, два Console App (C++), линковка ws2_32.lib

## Запуск
server.exe --host 127.0.0.1 --port 20090
client.exe --host 127.0.0.1 --port 20090 --id 1 --count 20