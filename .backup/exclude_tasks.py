"""
Halmet Task Exclusion Script
Removes unwanted task includes from the auto-generated GwUserTasks.h
to reduce heap usage. Only runs for halmet environment.

This script runs as a POST script after the main extra_script.py
"""
import os
import sys

# Import is provided by PlatformIO/SCons at runtime
Import("env")  # type: ignore # noqa: F821

# Only run for halmet environment
pioenv = env.get("PIOENV", "")  # type: ignore # noqa: F821
if pioenv != "halmet":
    print(f"Not halmet environment (current: {pioenv}), skipping task exclusion")
    sys.exit(0)

# Tasks to exclude (will be removed from GwUserTasks.h and compilation)
EXCLUDED_TASKS = [
    # 'GwButtonTask.h',
    # 'GwLedTask.h',
    'GwIicTask.h',
    'GwSpiTask.h',
    'GwTcpLogTask.h',
]

# Corresponding library directories to exclude from build
EXCLUDED_LIB_DIRS = [
    # 'buttontask',
    # 'ledtask',
    'iictask',
    'spitask',
    'tcplogtask',
]

def get_project_dir():
    """Get the project root directory"""
    return env.get("PROJECT_DIR")  # type: ignore # noqa: F821

def modify_user_tasks():
    """Modify the generated GwUserTasks.h to exclude certain tasks"""
    project_dir = get_project_dir()
    user_tasks_file = os.path.join(project_dir, "lib", "generated", "GwUserTasks.h")
    
    if not os.path.exists(user_tasks_file):
        print(f"[HALMET] ERROR: {user_tasks_file} does not exist!")
        print(f"[HALMET] Checking if lib/generated directory exists...")
        gen_dir = os.path.join(project_dir, "lib", "generated")
        if os.path.exists(gen_dir):
            print(f"[HALMET] Directory exists. Contents:")
            for f in os.listdir(gen_dir):
                print(f"[HALMET]   - {f}")
        else:
            print(f"[HALMET] Directory does not exist!")
        return False
    
    print(f"[HALMET] Found {user_tasks_file}, reading...")
    
    try:
        with open(user_tasks_file, 'r') as f:
            lines = f.readlines()
    except Exception as e:
        print(f"[HALMET] Error reading file: {e}")
        return False
    
    print(f"[HALMET] File has {len(lines)} lines. Content:")
    for line in lines:
        print(f"[HALMET]   {line.rstrip()}")
    
    modified_lines = []
    excluded_count = 0
    
    for line in lines:
        should_exclude = False
        for excluded_task in EXCLUDED_TASKS:
            if excluded_task in line and '#include' in line:
                should_exclude = True
                excluded_count += 1
                print(f"[HALMET]   - Excluding: {excluded_task}")
                break
        
        if not should_exclude:
            modified_lines.append(line)
    
    # Only write if we actually excluded something
    if excluded_count > 0:
        try:
            with open(user_tasks_file, 'w') as f:
                f.writelines(modified_lines)
            print(f"[HALMET] Successfully excluded {excluded_count} tasks from GwUserTasks.h")
            return True
        except Exception as e:
            print(f"[HALMET] Error writing file: {e}")
            return False
    else:
        print(f"[HALMET] No tasks found to exclude (this might indicate the file hasn't been generated yet)")
        return False

# Flag to ensure we only run once
_has_run = False

# Schedule to run when the build directory is being set up
def run_task_exclusion(source, target, env):  # type: ignore # noqa: F821
    global _has_run
    if _has_run:
        return
    _has_run = True
    print("[HALMET] Task exclusion callback triggered...")
    # Give the pre-script a moment to finish if needed
    import time
    time.sleep(0.1)
    modify_user_tasks()


# Try to hook early in the build process
# AddPreAction on a build output triggers before that target is built
try:
    env.AddPreAction("$BUILD_DIR/src/main.cpp.o", run_task_exclusion)  # type: ignore # noqa: F821
    print("[HALMET] Task exclusion script loaded - will run before main.cpp compilation")
except:
    print("[HALMET] Warning: Could not register pre-action hook")
