# Testing Task Exclusion

## How to test if it's working:

1. **Clean build first:**
   ```bash
   pio run -e halmet --target clean
   ```

2. **Build with verbose output:**
   ```bash
   pio run -e halmet -v
   ```

3. **Look for these messages in the output:**
   ```
   [HALMET] Task exclusion script running...
   [HALMET] Modifying .../lib/generated/GwUserTasks.h to exclude tasks...
   [HALMET]   - Excluding: GwButtonTask.h
   [HALMET]   - Excluding: GwLedTask.h
   [HALMET]   - Excluding: GwIicTask.h
   [HALMET]   - Excluding: GwSpiTask.h
   [HALMET]   - Excluding: GwTcpLogTask.h
   [HALMET] Successfully excluded 5 tasks from GwUserTasks.h
   ```

4. **Verify the generated file:**
   ```bash
   cat lib/generated/GwUserTasks.h
   ```
   
   Should NOT contain:
   - `#include <GwButtonTask.h>`
   - `#include <GwLedTask.h>`
   - `#include <GwIicTask.h>`
   - `#include <GwSpiTask.h>`
   - `#include <GwTcpLogTask.h>`

## Troubleshooting:

### If you see "will be created by main script"
- The script ran before `extra_script.py` generated the file
- This is normal for the first pass
- Try: `pio run -e halmet -t clean && pio run -e halmet`

### If you see "No tasks found to exclude"
- The file exists but doesn't contain the task headers
- Check if tasks are named differently
- Look at the actual content of `lib/generated/GwUserTasks.h`

### If it still doesn't work:
1. Make sure you're building the `halmet` environment
2. Check that `lib/halmettask/platformio.ini` has:
   ```ini
   extra_scripts = 
       post:lib/halmettask/exclude_tasks.py
   ```
3. Verify the script has execution permissions (shouldn't matter for Python but check anyway)
