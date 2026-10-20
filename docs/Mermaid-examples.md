# Mermaid in Markdown_with_Mermaid

These diagrams render locally. Use the plugin's theme button to switch both
Preview and diagram colors between light and dark.

## Processing flow

```mermaid
flowchart LR
    A[Markdown file] --> B{Contains diagrams?}
    B -->|Yes| C[Mermaid]
    B -->|No| D[Plain text]
    C --> E[HTML page]
    D --> E
```

## Sequence

```mermaid
sequenceDiagram
    participant U as User
    participant TC as Notepad++
    participant MV as Markdown_with_Mermaid
    U->>TC: Ctrl+Alt+M
    TC->>MV: Open Markdown
    MV-->>U: Show the page with diagrams
```

## Entity relationships

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

## Classes

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

## Work plan

```mermaid
gantt
    title Mermaid support
    dateFormat YYYY-MM-DD
    section Development
    Integration :done, a, 2026-10-17, 1d
    Validation  :b, after a, 1d
```

## Pie chart

```mermaid
pie title Document content
    "Text" : 60
    "Diagrams" : 30
    "Code" : 10
```

## Text diagrams stay as text

```text
┌──────────┐     ┌──────────┐
│ Markdown │ ──► │   HTML   │
└──────────┘     └──────────┘
```
