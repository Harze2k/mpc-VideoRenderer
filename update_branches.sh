#!/bin/bash

TARGET_BRANCHES=("hdr-enhancement" "hdr-enhancement-v2" "master")
DIRECTORIES_TO_SYNC=("Source" "Shaders/d3d11" ".github/workflows")

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

set -e

[[ $# -eq 0 ]] && { echo -e "${RED}Usage: $0 \"Commit message\"${NC}"; exit 1; }
COMMIT_MESSAGE="$1"

[[ ! -d ".git" ]] && { echo -e "${RED}Error: Run from the Git repository root.${NC}"; exit 1; }

ORIGINAL_BRANCH=$(git rev-parse --abbrev-ref HEAD)

echo -e "${YELLOW}--- Fetching remote updates ---${NC}"
git fetch origin

[[ ! -d "Dev" ]] && { echo -e "${RED}Error: 'Dev' directory not found.${NC}"; exit 1; }
git add .gitignore
git add .
if git diff --staged --quiet; then
	echo -e "${YELLOW}No changes to commit.${NC}"
	exit 0
fi

echo -e "${CYAN}--- Committing and force-pushing '$ORIGINAL_BRANCH' ---${NC}"
git commit -m "$COMMIT_MESSAGE"
COMMIT_HASH=$(git rev-parse HEAD)
git push origin "$ORIGINAL_BRANCH" --force

for branch in "${TARGET_BRANCHES[@]}"; do
	[[ "$branch" == "$ORIGINAL_BRANCH" ]] && continue

	echo -e "${CYAN}>>> Processing '$branch' <<<${NC}"
	git checkout "$branch"
	git reset --hard "origin/$branch"

	for dir in "${DIRECTORIES_TO_SYNC[@]}"; do
		rm -rf "$dir"
		git checkout "$COMMIT_HASH" -- "$dir"
	done

	rm -rf Dev
	git add .

	if git diff --staged --quiet; then
		echo -e "${YELLOW}No changes to commit on '$branch'.${NC}"
		continue
	fi

	git commit -m "$COMMIT_MESSAGE"
	git push origin "$branch" --force
done

git checkout "$ORIGINAL_BRANCH"
echo -e "${GREEN}✅ Deployment complete!${NC}"
