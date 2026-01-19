Import("env")
import os
import subprocess

def ensure_git_head_ref(build_dir):
    """Create git-data/head-ref file to prevent CMake git revision detection errors."""
    # Try to get actual git hash, fallback to dummy hash if git fails
    git_hash = "0000000000000000000000000000000000000000"
    try:
        project_dir = env.subst("$PROJECT_DIR")
        result = subprocess.run(
            ['git', 'rev-parse', 'HEAD'],
            cwd=project_dir,
            capture_output=True,
            text=True,
            timeout=5
        )
        if result.returncode == 0 and result.stdout.strip():
            git_hash = result.stdout.strip()
    except (subprocess.TimeoutExpired, FileNotFoundError, subprocess.SubprocessError):
        pass

    # Create git-data directory and head-ref file for main project
    git_data_dir = os.path.join(build_dir, "CMakeFiles", "git-data")
    os.makedirs(git_data_dir, exist_ok=True)
    head_ref_path = os.path.join(git_data_dir, "head-ref")
    with open(head_ref_path, "w") as f:
        f.write(git_hash + "\n")

    # Create git-data directory and head-ref file for bootloader subproject
    bootloader_git_data_dir = os.path.join(build_dir, "bootloader", "CMakeFiles", "git-data")
    os.makedirs(bootloader_git_data_dir, exist_ok=True)
    bootloader_head_ref_path = os.path.join(bootloader_git_data_dir, "head-ref")
    with open(bootloader_head_ref_path, "w") as f:
        f.write(git_hash + "\n")

# Run before CMake configuration
# BUILD_DIR might not exist yet, so we construct it from PROJECT_DIR
project_dir = env.subst("$PROJECT_DIR")
build_dir = env.subst("$BUILD_DIR")
if not build_dir:
    # Fallback: construct expected build directory path
    build_dir = os.path.join(project_dir, ".pio", "build", env.subst("$PIOENV"))

if build_dir:
    # Ensure parent directories exist
    os.makedirs(build_dir, exist_ok=True)
    ensure_git_head_ref(build_dir)
