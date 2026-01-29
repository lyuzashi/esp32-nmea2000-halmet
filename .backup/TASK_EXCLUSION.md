# Halmet Task Exclusion System

## Overview
This directory contains a custom build script (`exclude_tasks.py`) that reduces heap usage by excluding unnecessary tasks from the build without modifying core source files.

## How It Works

1. The main `extra_script.py` runs and generates `lib/generated/GwUserTasks.h` with all tasks
2. The `exclude_tasks.py` script (configured in `platformio.ini`) runs next
3. It modifies the generated `GwUserTasks.h` to remove includes for unwanted tasks
4. The build proceeds without those tasks being compiled or linked

## Excluded Tasks

The following tasks are excluded to save memory:
- `GwButtonTask` - Button handling (not needed without physical buttons)
- `GwLedTask` - LED control (not needed)  
- `GwIicTask` - I2C sensor support (not needed)
- `GwSpiTask` - SPI sensor support (not needed)
- `GwTcpLogTask` - TCP logging (disabled by default anyway)

## To Modify

Edit `exclude_tasks.py` and change the `EXCLUDED_TASKS` list:

```python
EXCLUDED_TASKS = [
    'GwButtonTask.h',
    'GwLedTask.h',
    # Add or remove task headers here
]
```

## Benefits

- No modification of core codebase required
- Changes isolated to `lib/halmettask/` directory
- Easy to maintain and version control
- Significantly reduces heap usage
- Can be easily adjusted per environment
