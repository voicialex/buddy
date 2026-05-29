#!/usr/bin/env bash
set -euo pipefail

# Code formatting and linting script
#
# Usage:
#   ./format_and_lint.sh          # Format + lint
#   ./format_and_lint.sh --check  # Dry-run (exit 1 if unformatted)
#
# Config files:
#   .clang-format        — C++ style (Google-based, 120 col)
#   .cmake-format.yaml   — CMake style

readonly TARGET_DIR="src"
readonly CHECK_ONLY="${1:---format}"

# ── Colors ──

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'
BOLD='\033[1m'

print_sep() { echo -e "${BOLD}${CYAN}==========================================${NC}"; }

print_sep
echo -e "${BOLD}${CYAN}Code Formatting and Linting Tool${NC}"
echo -e "${CYAN}Target: ${TARGET_DIR}/${NC}"
print_sep
echo ""

# ── Tool check ──

missing=()
for tool in clang-format cmake-format; do
  command -v "$tool" &>/dev/null || missing+=("$tool")
done
if (( ${#missing[@]} )); then
  echo -e "${RED}Missing tools: ${missing[*]}${NC}"
  echo -e "${YELLOW}Install: pip install ${missing[*]}${NC}"
  exit 1
fi

# ── Generic formatter ──

run_formatter() {
  local title="$1"
  shift
  local tool_cmd=("$@")

  local files=()
  while IFS= read -r f; do
    [[ -f "$f" ]] && files+=("$f")
  done

  echo -e "${BOLD}${BLUE}${title}${NC}"
  if (( ${#files[@]} == 0 )); then
    echo -e "${YELLOW}  No files found.${NC}"
    echo ""
    return 0
  fi
  echo -e "${CYAN}  Files: ${#files[@]}${NC}"

  local formatted=0 passed=0 errors=0
  for file in "${files[@]}"; do
    local tmp
    tmp=$(mktemp)
    if "${tool_cmd[@]}" "$file" > "$tmp" 2>/dev/null; then
      if ! cmp -s "$file" "$tmp"; then
        if [[ "$CHECK_ONLY" == "--check" ]]; then
          echo -e "  ${RED}✗${NC} $file"
          ((errors++))
        else
          mv "$tmp" "$file"
          echo -e "  ${GREEN}✓${NC} $file"
          ((formatted++))
          continue
        fi
      else
        ((passed++))
      fi
    else
      echo -e "  ${RED}✗${NC} $file (tool error)"
      ((errors++))
    fi
    rm -f "$tmp"
  done

  (( formatted )) && echo -e "${GREEN}  Formatted: $formatted${NC}"
  (( passed ))    && echo -e "${GREEN}  Already clean: $passed${NC}"
  (( errors ))    && echo -e "${RED}  Errors: $errors${NC}"
  echo ""
  return $errors
}

# ── Formatters ──

format_cpp() {
  find "$TARGET_DIR" -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.h' -o -name '*.hpp' \) \
    | run_formatter "Formatting C++..." clang-format --style=file
}

format_cmake() {
  find "$TARGET_DIR" -type f \( -name 'CMakeLists.txt' -o -name '*.cmake' \) \
    | run_formatter "Formatting CMake..." cmake-format
}

# ── Main ──

exit_code=0

format_cpp   || exit_code=1
format_cmake || exit_code=1

print_sep
if (( exit_code == 0 )); then
  echo -e "${BOLD}${GREEN}All checks passed!${NC}"
else
  echo -e "${BOLD}${YELLOW}Completed with issues.${NC}"
fi
print_sep

exit $exit_code
