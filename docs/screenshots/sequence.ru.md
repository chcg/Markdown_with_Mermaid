# Понятно с первого взгляда

Диаграмма последовательности превращает рабочий процесс в наглядную историю.

```mermaid
sequenceDiagram
    actor Автор
    participant Editor as Notepad++
    participant Preview as Предпросмотр
    participant Reader as Читатель
    Автор->>Editor: Markdown + Mermaid
    Editor->>Preview: Несохранённые правки
    activate Preview
    Preview-->>Автор: Текст и диаграммы
    deactivate Preview
    Note over Автор,Preview: Отправить только нужное
    Preview->>Reader: HTML, PDF или DOCX
```
