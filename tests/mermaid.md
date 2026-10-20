# Mermaid в Markdown_with_Mermaid

Схемы ниже строятся локально. Сменить тему вместе с цветами схем можно в меню плагина.

## Поток обработки

```mermaid
flowchart LR
    A[Markdown-файл] --> B{Есть схемы?}
    B -->|Да| C[Mermaid]
    B -->|Нет| D[Обычный текст]
    C --> E[HTML-страница]
    D --> E
```

## Последовательность

```mermaid
sequenceDiagram
    participant U as Пользователь
    participant TC as Notepad++
    participant MV as Markdown_with_Mermaid
    U->>TC: Ctrl+Alt+M
    TC->>MV: Открыть Markdown
    MV-->>U: Показать страницу со схемами
```

## Связи таблиц

```mermaid
erDiagram
    DOCUMENT ||--o{ DIAGRAM : contains
    DOCUMENT {
        int id PK
        string filename
    }
    DIAGRAM {
        int id PK
        string type
    }
```

## Классы

```mermaid
classDiagram
    class Viewer {
        +open(file)
        +render()
    }
    class Markdown
    class Mermaid
    Viewer --> Markdown
    Viewer --> Mermaid
```

## План работ

```mermaid
gantt
    title Поддержка Mermaid
    dateFormat YYYY-MM-DD
    section Разработка
    Интеграция :done, a, 2026-10-17, 1d
    Проверка   :b, after a, 1d
```

## Круговая диаграмма

```mermaid
pie title Содержимое документа
    "Текст" : 60
    "Схемы" : 30
    "Код" : 10
```

## Псевдографика остаётся текстом

```text
┌──────────┐     ┌──────────┐
│ Markdown │ ──► │   HTML   │
└──────────┘     └──────────┘
```
