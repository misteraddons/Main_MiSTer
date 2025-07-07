## Git Workflow
- Always create a new branch from the misteraddons/master branch, and publish the repository to my fork of Main MiSTer

## Development Preferences
- Use unix line endings

## Debug Memories
- Found an issue with menu scrolling animation refresh: Timer-based refresh in MENU_CORE_CATEGORY2 caused infinite menu refresh loop
  - Problem: CheckTimer(0) returns true immediately, forcing constant menu state transition
  - Symptom: Menu would appear to "fall through" or exit unexpectedly
  - The code was forcibly refreshing menu every 100ms, preventing user interaction