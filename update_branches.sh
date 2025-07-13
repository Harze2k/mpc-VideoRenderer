#!/bin/bash

# --- Configuration ---
TARGET_BRANCHES=("hdr-enhancement" "hdr-enhancement-v2" "master")
DIRECTORIES_TO_SYNC=(
    "Source"
    "Shaders/d3d11"
    ".github/workflows"
)
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

set -e

if [ $# -eq 0 ]; then
  echo -e "${RED}Error: Please provide a commit message in quotes.${NC}"
  echo "Usage: ./deploy_folders.sh \"Your deployment description\""
  exit 1
fi
COMMIT_MESSAGE="$1"

if [ ! -d ".git" ]; then
    echo -e "${RED}Error: Not a Git repository. Run from the project root.${NC}"
    exit 1
fi

ORIGINAL_BRANCH=$(git rev-parse --abbrev-ref HEAD)
echo -e "${YELLOW}--- Updating remote information ---${NC}"
git fetch origin

# --- EXCLUDE DEV FOLDER ---
if [ -d "Dev" ]; then
    git update-index --assume-unchanged Dev/*
fi

echo -e "${YELLOW}--- Staging ALL local changes for the definitive commit ---${NC}"
git add .
# Unstage any Dev files if staged by accident
git reset Dev > /dev/null 2>&1 || true

if git diff --staged --quiet; then
    echo -e "${YELLOW}No changes to commit. Nothing to deploy. Exiting.${NC}"
    # Reset exclusion for Dev in case of future work
    if [ -d "Dev" ]; then
        git update-index --no-assume-unchanged Dev/*
    fi
    exit 0
fi

echo -e "${YELLOW}--- Creating the definitive commit on branch '$ORIGINAL_BRANCH' ---${NC}"
git commit -m "$COMMIT_MESSAGE"
COMMIT_HASH=$(git rev-parse HEAD)
echo -e "Definitive commit created: ${GREEN}$COMMIT_HASH${NC}"

echo -e "\n${CYAN}--- Force-pushing definitive commit to '$ORIGINAL_BRANCH' ---${NC}"
git push origin "$ORIGINAL_BRANCH" --force

for branch in "${TARGET_BRANCHES[@]}"; do
  if [ "$branch" == "$ORIGINAL_BRANCH" ]; then
      echo -e "\n${YELLOW}Skipping '$branch' as it's the current (and now pushed) branch.${NC}"
      continue
  fi

  echo -e "\n${CYAN}>>> Processing Branch: $branch <<<${NC}"
  echo "Switching to '$branch' and resetting to match remote 'origin/$branch'..."
  git checkout "$branch"
  git reset --hard "origin/$branch"

  echo "Overwriting target folders with content from commit $COMMIT_HASH..."
  for dir in "${DIRECTORIES_TO_SYNC[@]}"; do
    echo "  Syncing folder: '$dir'"
    rm -rf "$dir"
    git checkout "$COMMIT_HASH" -- "$dir"
  done

  # --- ENSURE DEV IS NOT TOUCHED ---
  rm -rf Dev

  echo "Staging all synchronized changes..."
  git add .
  git reset Dev > /dev/null 2>&1 || true

  if git diff --staged --quiet; then
      echo -e "${YELLOW}No effective changes on branch '$branch'. The folders were already in sync.${NC}"
      continue
  fi

  echo "Creating sync commit on '$branch'..."
  git commit -m "$COMMIT_MESSAGE"
  echo -e "${RED}Force-pushing '$branch' to origin...${NC}"
  git push origin "$branch" --force
done

echo -e "\n${CYAN}--- Deployment Complete. Returning to original branch ($ORIGINAL_BRANCH) ---${NC}"
git checkout "$ORIGINAL_BRANCH"

# --- REMOVE EXCLUSION ON DEV FOLDER ---
if [ -d "Dev" ]; then
    git update-index --no-assume-unchanged Dev/*
fi

echo -e "\n${GREEN}✅ All target branches have been updated and force-pushed successfully!${NC}"
