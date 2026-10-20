# PDF export — проверка кириллицы

PDF_TEXT_LAYER: This text must remain selectable. Русский текст и числа 12345.

**Жирный текст**, *курсив*, [пример ссылки](https://example.com).

## Локальное изображение

![Локальное изображение](sample.svg)

## Ошибочная схема и продолжение

```mermaid
flowchart TD
  A[Unclosed
```

```mermaid
flowchart LR
  A[После ошибки] --> B[Продолжаем экспорт]
```

```text
LONG_CODE_LINE_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz
```

![Недоступная картинка](pdf-image-that-does-not-exist.png)

## Таблица на нескольких страницах

| Number | Marker | Value |
| --- | --- | --- |
