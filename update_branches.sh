#!/bin/bash

# --- Configuration ---
# The branches to which the directories will be deployed.
TARGET_BRANCHES=("hdr-enhancement" "hdr-enhancement-v2" "master")

# The directories to sync.
# This script will make these directories on the target branches
# identical to their state in your *final commit* on this branch.
DIRECTORIES_TO_SYNC=(
    "Source"
    "Shaders/d3d11"
    ".github/workflows"
)

# --- Style Definitions ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Exit immediately if a command exits with a non-zero status.
set -e

# --- Script Logic ---

# 1. Check for commit message
if [ $# -eq 0 ]; then
  echo -e "${RED}Error: Please provide a commit message in quotes.${NC}"
  echo "Usage: ./deploy_folders.sh \"Your deployment description\""
  exit 1
fi
COMMIT_MESSAGE="$1"

# 2. Verify we are in a git repository
if [ ! -d ".git" ]; then
    echo -e "${RED}Error: Not a Git repository. Run from the project root.${NC}"
    exit 1
fi

# 3. Get the original branch
ORIGINAL_BRANCH=$(git rev-parse --abbrev-ref HEAD)

# 4. Create the definitive commit on the current branch
echo -e "${YELLOW}--- Staging ALL local changes for the definitive commit ---${NC}"
git add .

if git diff --staged --quiet; then
    echo -e "${YELLOW}No changes to commit. Nothing to deploy. Exiting.${NC}"
    exit 0
fi

echo -e "${YELLOW}--- Creating the definitive commit on branch '$ORIGINAL_BRANCH' ---${NC}"
git commit -m "$COMMIT_MESSAGE"
COMMIT_HASH=$(git rev-parse HEAD)
echo -e "Definitive commit created: ${GREEN}$COMMIT_HASH${NC}"

# 5. Push the definitive commit to the original branch first
echo -e "\n${CYAN}--- Pushing definitive commit to '$ORIGINAL_BRANCH' ---${NC}"
git push origin "$ORIGINAL_BRANCH"

# 6. Loop through target branches
for branch in "${TARGET_BRANCHES[@]}"; do
  # Skip if the target is the branch we're already on
  if [ "$branch" == "$ORIGINAL_BRANCH" ]; then
      echo -e "\n${YELLOW}Skipping '$branch' as it's the current (and now pushed) branch.${NC}"
      continue
  fi

  echo -e "\n${CYAN}>>> Processing Branch: $branch <<<${NC}"

  # Get a clean, up-to-date version of the target branch
  echo "Switching to '$branch' and syncing with origin..."
  git checkout "$branch"
  git fetch origin
  git reset --hard "origin/$branch"

  # --- Overwrite folders with content from the definitive commit ---
  echo "Overwriting target folders with content from commit $COMMIT_HASH..."
  for dir in "${DIRECTORIES_TO_SYNC[@]}"; do
    echo "  Syncing folder: '$dir'"
    # Delete the folder on the current branch to ensure a clean slate
    rm -rf "$dir"
    # Use git to checkout the entire folder from the specific commit
    # This will restore the folder exactly as it was in that commit
    git checkout "$COMMIT_HASH" -- "$dir"
  done

  # --- Commit and Force Push ---
  echo "Staging all synchronized changes..."
  git add .

  if git diff --staged --quiet; then
      echo -e "${YELLOW}No effective changes on branch '$branch'. The folders were already in sync.${NC}"
      continue
  fi
  
  echo "Creating sync commit on '$branch'..."
  git commit -m "$COMMIT_MESSAGE"

  echo -e "${RED}Force-pushing '$branch' to origin...${NC}"
  git push origin "$branch" --force
done

# 7. Return to the original branch
echo -e "\n${CYAN}--- Deployment Complete. Returning to original branch ($ORIGINAL_BRANCH) ---${NC}"
git checkout "$ORIGINAL_BRANCH"

echo -e "\n${GREEN}✅ All target branches have been updated and force-pushed successfully!${NC}"