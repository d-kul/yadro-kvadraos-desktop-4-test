# Тестовое задание

Приложение на языке С++ под Linux, которое через равные интервалы времени обходит домашний каталог и находит в нем мультимедийные файлы (изображения, аудио, видео) и формирует файл в формате json следующего вида:

```json
{ "audio": [ "111.mp3", "222.wav" ], "video": [ "333.mpg" ] "images": [ "444.jpeg", "555.png" ] }
```

Файл доступен через HTTP по адресу `http://localhost:1234/media_files` через GET-запрос (с параметрами по умолчанию)

# Решение

Решение представляет собой проект CMake с использованием библиотеки [nlohmann/json](https://github.com/nlohmann/json/tree/develop).

# Сборка

```
cmake -S . -B build
cmake --build build
```

# Запуск на тестовых данных

```
build/mmcat --directory data/test
```

# Запуск

```
build/mmcat --directory <directory> [--interval <milliseconds> --port <port>]
```
