# VS Code + GitHub Copilot Workflow — Audiocity

## Where to place these files
Place this zip’s contents at the **root** of your project folder (e.g., `Audiocity/`).

## Initialize the project folder & repository
```bash
mkdir Audiocity
cd Audiocity
git init
```

Create folders for code and dependencies:
```bash
mkdir -p src tests third_party
```

## Open in VS Code
- File → Open Folder… → `Audiocity`
- Install extensions: GitHub Copilot, GitHub Copilot Chat, (optional) CMake Tools

## Prompting Copilot
Run prompts from `prompts/` in order.
