#!/usr/bin/env python3
import argparse
import subprocess
import os
import shutil
import sys

# --- Configuration ---
APP_IMAGE_TAG_LOCAL = "cherryrecorder-server:latest"
APP_IMAGE_TAG_K8S = "cherryrecorder-server:k8s-latest"
TEST_IMAGE_TAG = "cherryrecorder-server-test:latest"
APP_CONTAINER_NAME = "cherryrecorder-server-container"
APP_DOCKERFILE = "Dockerfile"
TEST_DOCKERFILE = "Dockerfile.test"
TEST_DOCKERIGNORE = "Dockerfile.test.dockerignore"
DEFAULT_DOCKERIGNORE = ".dockerignore"
BACKUP_DOCKERIGNORE = ".dockerignore.original"

# --- Port Mapping (Host:Container) - Only for Application Mode ---
HOST_PORT_HTTP = "8080"
CONTAINER_PORT_HTTP = "8080"
HOST_PORT_CHAT = "33334"
CONTAINER_PORT_CHAT = "33334"
HOST_PORT_ECHO = "33333" # Optional echo server port
CONTAINER_PORT_ECHO = "33333"

# --- Environment Variables File - Only for Application Mode ---
ENV_FILE_PATH = ".env"

# --- Helper Functions ---
def run_command(command, check=True, **kwargs):
    """Runs a shell command and optionally checks for errors."""
    print(f"\n---> Running command: {' '.join(command)}")
    try:
        # Use shell=True on Windows if needed, but avoid if possible
        use_shell = sys.platform == "win32"
        # Pass the 'check' argument received by the function
        result = subprocess.run(command, check=check, text=True, shell=use_shell, **kwargs)
        print(f"---> Command successful.")
        return True
    except subprocess.CalledProcessError as e:
        # Only print error if check=True
        if check:
            print(f"ERROR: Command failed with exit code {e.returncode}", file=sys.stderr)
            print(f"ERROR details: {e}", file=sys.stderr)
        else:
            # Optionally log non-fatal errors when check=False
            print(f"---> Command finished with non-zero exit code {e.returncode} (check=False, ignored).")
        # Return False only if check=True caused the exception
        return not check
    except FileNotFoundError:
        print(f"ERROR: Command not found: {command[0]}", file=sys.stderr)
        print("Ensure Docker CLI is installed and in your PATH.", file=sys.stderr)
        return False

def prepare_dockerignore_for_test():
    """Backs up .dockerignore and replaces it with the test version if needed."""
    print("Preparing .dockerignore for TEST build...")
    backup_made = False
    # Cleanup any lingering backup
    if os.path.exists(BACKUP_DOCKERIGNORE):
        try:
            os.remove(BACKUP_DOCKERIGNORE)
        except OSError as e:
            print(f"WARNING: Could not remove lingering backup {BACKUP_DOCKERIGNORE}: {e}", file=sys.stderr)

    if not os.path.exists(TEST_DOCKERIGNORE):
        print(f"WARNING: {TEST_DOCKERIGNORE} not found. Build will use {DEFAULT_DOCKERIGNORE} (if it exists).")
        return False # Indicate no backup needed

    if os.path.exists(DEFAULT_DOCKERIGNORE):
        print(f"  Backing up {DEFAULT_DOCKERIGNORE} to {BACKUP_DOCKERIGNORE}")
        try:
            shutil.move(DEFAULT_DOCKERIGNORE, BACKUP_DOCKERIGNORE)
            backup_made = True
        except OSError as e:
            print(f"ERROR: Failed to backup {DEFAULT_DOCKERIGNORE}: {e}", file=sys.stderr)
            raise # Re-raise the exception to stop the process

    print(f"  Copying {TEST_DOCKERIGNORE} to {DEFAULT_DOCKERIGNORE}...")
    try:
        shutil.copy2(TEST_DOCKERIGNORE, DEFAULT_DOCKERIGNORE) # copy2 preserves metadata
    except OSError as e:
        print(f"ERROR: Failed to copy {TEST_DOCKERIGNORE} to {DEFAULT_DOCKERIGNORE}: {e}", file=sys.stderr)
        # Attempt to restore backup if one was made
        if backup_made:
            try:
                shutil.move(BACKUP_DOCKERIGNORE, DEFAULT_DOCKERIGNORE)
            except OSError as re:
                print(f"ERROR: Failed to restore backup {BACKUP_DOCKERIGNORE}: {re}", file=sys.stderr)
        raise # Re-raise the exception

    return backup_made

def restore_dockerignore(backup_made):
    """Restores the original .dockerignore file."""
    print("Restoring .dockerignore...")
    if backup_made:
        try:
            if os.path.exists(DEFAULT_DOCKERIGNORE):
                os.remove(DEFAULT_DOCKERIGNORE)
            shutil.move(BACKUP_DOCKERIGNORE, DEFAULT_DOCKERIGNORE)
            print(f"  Restored {DEFAULT_DOCKERIGNORE} from {BACKUP_DOCKERIGNORE}")
        except OSError as e:
            print(f"WARNING: Failed to restore {DEFAULT_DOCKERIGNORE} from backup: {e}", file=sys.stderr)
    else:
        # If we copied the test file but no original existed, remove the copied one
        # We check if the copied file *is* the test one by comparing content (more robust than just existence)
        copied_is_test = False
        if os.path.exists(DEFAULT_DOCKERIGNORE) and os.path.exists(TEST_DOCKERIGNORE):
            try:
                # Simple content check (may fail for large files, but okay for ignore files)
                 with open(DEFAULT_DOCKERIGNORE, 'r') as f1, open(TEST_DOCKERIGNORE, 'r') as f2:
                     if f1.read() == f2.read():
                         copied_is_test = True
            except IOError as e:
                 print(f"WARNING: Could not compare ignore files: {e}", file=sys.stderr)

        if copied_is_test:
             print(f"  Removing temporary {DEFAULT_DOCKERIGNORE} (copied from {TEST_DOCKERIGNORE})...")
             try:
                 os.remove(DEFAULT_DOCKERIGNORE)
             except OSError as e:
                 print(f"WARNING: Failed to remove temporary {DEFAULT_DOCKERIGNORE}: {e}", file=sys.stderr)


# --- Main Script Logic ---
def main():
    parser = argparse.ArgumentParser(description="Build and run/test CherryRecorder Server using Docker.")
    parser.add_argument(
        "--target",
        choices=["app", "test", "k8s"],
        default="app",
        help="Target to build: 'app' (default), 'test', or 'k8s' for Kubernetes deployment."
    )
    parser.add_argument(
        "--push",
        action="store_true",
        help="Push the built image to Docker Hub (requires docker login)."
    )
    parser.add_argument(
        "--dockerhub-username",
        help="Docker Hub username for tagging and pushing images."
    )
    args = parser.parse_args()

    backup_made = False
    try:
        # --- Prepare Environment based on Target ---
        if args.target == "test":
            print("--- Running in TEST mode ---")
            image_tag = TEST_IMAGE_TAG
            dockerfile = TEST_DOCKERFILE
            backup_made = prepare_dockerignore_for_test()
            build_args = ["docker", "build", "--pull", "-t", image_tag, "-f", dockerfile, "."]
            run_args = ["docker", "run", "--rm", "-e", "GOOGLE_MAPS_API_KEY=dummy_key", image_tag]
        elif args.target == "k8s":
            print("--- Building for K8S deployment ---")
            image_tag = APP_IMAGE_TAG_K8S
            if args.dockerhub_username:
                image_tag = f"{args.dockerhub_username}/{image_tag}"
            dockerfile = APP_DOCKERFILE
            build_args = ["docker", "build", "--pull", "-t", image_tag, "-f", dockerfile, "."]
            run_args = None  # K8s mode doesn't run containers locally
        else: # app mode
            print("--- Running in APPLICATION mode ---")
            image_tag = APP_IMAGE_TAG_LOCAL
            dockerfile = APP_DOCKERFILE
            container_name = APP_CONTAINER_NAME

            # Stop and remove existing app container
            print(f"Stopping and removing existing container '{container_name}' (if any)...")
            run_command(["docker", "kill", container_name], stderr=subprocess.DEVNULL, check=False) # Ignore errors if container doesn't exist
            run_command(["docker", "rm", container_name], stderr=subprocess.DEVNULL, check=False)    # Ignore errors if container doesn't exist

            build_args = ["docker", "build", "--pull", "-t", image_tag, "-f", dockerfile, "."]

            # Prepare run args for app
            run_args_list = ["docker", "run", "-d", "--name", container_name]
            port_map_options = [
                "-p", f"{HOST_PORT_HTTP}:{CONTAINER_PORT_HTTP}",
                "-p", f"{HOST_PORT_CHAT}:{CONTAINER_PORT_CHAT}"
            ]
            if HOST_PORT_ECHO: # Add echo port only if defined
                 port_map_options.extend(["-p", f"{HOST_PORT_ECHO}:{CONTAINER_PORT_ECHO}"])
            run_args_list.extend(port_map_options)

            if os.path.exists(ENV_FILE_PATH):
                print(f"Found environment file: {ENV_FILE_PATH}")
                run_args_list.extend(["--env-file", ENV_FILE_PATH])
            else:
                print(f"WARNING: Environment file not found at {ENV_FILE_PATH}. Container will run without it.")

            run_args_list.append(image_tag)
            run_args = run_args_list


        # --- Build Docker Image ---
        if not run_command(build_args):
            sys.exit(1) # Exit if build fails

        # --- Push to Docker Hub (if requested) ---
        if args.push and args.target in ["app", "k8s"]:
            if not args.dockerhub_username and args.target == "k8s":
                print("ERROR: --dockerhub-username is required for k8s target with --push", file=sys.stderr)
                sys.exit(1)
            
            push_tag = image_tag
            if args.target == "app" and args.dockerhub_username:
                # Tag local image for push
                push_tag = f"{args.dockerhub_username}/{APP_IMAGE_TAG_LOCAL}"
                if not run_command(["docker", "tag", image_tag, push_tag]):
                    print("ERROR: Docker tag failed!", file=sys.stderr)
                    sys.exit(1)
            
            print(f"\nPushing image to Docker Hub...")
            if not run_command(["docker", "push", push_tag]):
                print("ERROR: Docker push failed!", file=sys.stderr)
                sys.exit(1)
            print(f"Successfully pushed: {push_tag}")

        # --- Run Docker Container ---
        if run_args:
            if not run_command(run_args):
                 print(f"ERROR: Docker run failed or tests failed for target '{args.target}'!", file=sys.stderr)
                 sys.exit(1) # Exit if run/tests fail

        # --- Post-run messages ---
        if args.target == "app":
            print(f"\nContainer '{APP_CONTAINER_NAME}' started successfully.")
            print(f"  HTTP API:    http://localhost:{HOST_PORT_HTTP}")
            print(f"  Chat Server: localhost:{HOST_PORT_CHAT}")
            print(f"  Echo Server: localhost:{HOST_PORT_ECHO}")
            print(f"  To view logs: docker logs {APP_CONTAINER_NAME} -f")
            print(f"  To stop:      docker kill {APP_CONTAINER_NAME}")
        elif args.target == "test":
            print("\nTests completed successfully.")
        elif args.target == "k8s":
            print(f"\nK8s image built successfully: {image_tag}")
            print("To deploy to Kubernetes:")
            print(f"  1. Update your deployment YAML with image: {image_tag}")
            print(f"  2. kubectl apply -f <your-deployment.yaml>")

        print("\nScript finished.")

    except Exception as e:
        print(f"\nAn unexpected error occurred: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        # --- Ensure .dockerignore is restored ---
        if args.target == "test": # Only restore if we potentially modified it
             restore_dockerignore(backup_made)

if __name__ == "__main__":
    main()
