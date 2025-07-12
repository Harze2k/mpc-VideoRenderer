#!/bin/bash

# --- Configuration ---
TARGET_BRANCHES=("hdr-enhancement" "hdr-enhancement-v2" "master")

# --- Style Definitions ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Exit immediately if a command exits with a non-zero status.
set -e

# --- Script Logic ---

# 1. Check for commit message
if [ $# -eq 0 ]; then
  echo -e "${RED}Error: Please provide a commit message in quotes.${NC}"
  echo "Usage: ./update_branches.sh \"Your commit message\""
  exit 1
fi
COMMIT_MESSAGE="$1"

# 2. Ensure we are in a git repository
if [ ! -d ".git" ]; then
    echo -e "${RED}Error: Not a Git repository. Run from project root.${NC}"
    exit 1
fi

# 3. Get the current branch name
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
echo -e "${YELLOW}You are on branch: ${GREEN}$CURRENT_BRANCH${NC}"
echo -e "${YELLOW}A commit will be created and then cherry-picked onto: ${GREEN}${TARGET_BRANCHES[*]}${NC}"
echo ""

# 4. Add all changes and create the master commit
echo -e "${GREEN}--- Staging all current changes ---${NC}"
git add .

if git diff --staged --quiet; then
    echo -e "${YELLOW}No changes to commit. Exiting.${NC}"
    exit 0
fi

echo -e "${GREEN}--- Creating commit on '$CURRENT_BRANCH' ---${NC}"
git commit -m "$COMMIT_MESSAGE"

# 5. Get the hash of the commit we just created
LATEST_COMMIT_HASH=$(git rev-parse HEAD)
echo -e "${YELLOW}Commit to apply: ${GREEN}$LATEST_COMMIT_HASH${NC}"

# 6. Push the current branch first
echo -e "\n${GREEN}--- Pushing '$CURRENT_BRANCH' ---${NC}"
git push origin "$CURRENT_BRANCH"

# 7. Loop through target branches to cherry-pick
for branch in "${TARGET_BRANCHES[@]}"; do
  if [ "$branch" == "$CURRENT_BRANCH" ]; then
      echo -e "\n${YELLOW}Skipping '$branch' as it's the current branch.${NC}"
      continue
  fi

  echo -e "\n${GREEN}--- Updating branch: $branch ---${NC}"

  # Switch to the target branch
  git checkout "$branch"
  
  # Ensure the local branch is up-to-date with the remote
  echo "Pulling latest changes for '$branch'..."
  git pull origin "$branch"

  # Apply the specific commit
  echo "Cherry-picking commit ${LATEST_COMMIT_HASH} onto '$branch'..."
  git cherry-pick "$LATEST_COMMIT_HASH"
  
  # Push the updated branch
  echo "Pushing '$branch' to origin..."
  git push origin "$branch"
done

# 8. Return to the original branch
echo -e "\n${GREEN}--- Returning to original branch ($CURRENT_BRANCH) ---${NC}"
git checkout "$CURRENT_BRANCH"

echo -e "\n${GREEN}✅ All branches have been updated successfully!${NC}"